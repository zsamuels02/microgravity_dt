% =========================================================
%  DROP TOWER — Ground Station Serial Logger
% =========================================================
%  SETUP:
%    1. Plug in the ground station Feather via USB
%    2. Set COM_PORT below
%    3. Run this script
%    4. Use the buttons on the live plot to control the DV
%
%  OUTPUT:
%    Saves drop_log.csv to the same folder as this script
% =========================================================

COM_PORT  = 'COM9';       % <-- change to ywour port
BAUD_RATE = 115200;
LOG_FILE  = ['friday_drop_log_' char(datetime('now', 'Format', 'yyyy-MM-dd_HH-mm-ss')) '.csv'];  

% LiPo battery voltage thresholds
BATT_FULL  = 4.2;   % V
BATT_EMPTY = 3.4;   % V (safe cutoff)

% ── Open serial port ──────────────────────────────────────────
s = serialport(COM_PORT, BAUD_RATE);
configureTerminator(s, "LF");
flush(s);
disp('Connected. Use the buttons in the figure to control the DV.')

% ── Open log file ─────────────────────────────────────────────
fid = fopen(LOG_FILE, 'w');
fprintf(fid, 'ts_ms,ax_g,ay_g,az_g,gx_dps,gy_dps,gz_dps,imu_temp_c,ambient_temp_c,freefall,logging,samples,battery_v,esp32_temp_c\n');

% ── Figure + buttons ──────────────────────────────────────────
fig = figure('Name', 'Drop Tower Ground Station', 'Position', [100 100 960 520]);

% Live plot — slightly narrower to make room for battery panel
ax_plot = axes(fig, 'Position', [0.08 0.18 0.72 0.74]);
xlabel('Time (s)'); ylabel('Accel Z (g)');
title('Live Accel Z — each color is a new session');
grid on; hold on;

% ── Battery display panel (top right) ─────────────────────────
% Percentage text
batt_pct_txt = uicontrol(fig, 'Style', 'text', ...
    'Position', [790 430 150 30], ...
    'String', 'Battery: --', ...
    'FontSize', 11, 'FontWeight', 'bold', ...
    'HorizontalAlignment', 'center');

% Voltage text
batt_v_txt = uicontrol(fig, 'Style', 'text', ...
    'Position', [790 405 150 22], ...
    'String', '-- V', ...
    'FontSize', 10, ...
    'HorizontalAlignment', 'center');

% Battery bar — outer border
uicontrol(fig, 'Style', 'frame', ...
    'Position', [810 240 110 160], ...
    'BackgroundColor', [0.3 0.3 0.3]);

% Battery bar — fill (will resize dynamically)
batt_bar = uicontrol(fig, 'Style', 'frame', ...
    'Position', [812 242 106 0], ...   % height starts at 0
    'BackgroundColor', [0.2 0.8 0.2]);

% Battery tip (the little nub on top)
uicontrol(fig, 'Style', 'frame', ...
    'Position', [840 402 50 10], ...
    'BackgroundColor', [0.3 0.3 0.3]);

% ── Status + command buttons ──────────────────────────────────
status_txt = uicontrol(fig, 'Style', 'text', ...
    'Position', [10 5 400 20], ...
    'String', 'Status: waiting for DV...', ...
    'HorizontalAlignment', 'left');

uicontrol(fig, 'Style', 'pushbutton', 'String', 'START', ...
    'Position', [540 5 100 28], 'ForegroundColor', [0 0.6 0], ...
    'FontWeight', 'bold', ...
    'Callback', @(~,~) sendCmd(s, 's', status_txt, 'Logging STARTED'));

uicontrol(fig, 'Style', 'pushbutton', 'String', 'STOP', ...
    'Position', [650 5 100 28], 'ForegroundColor', [0.8 0 0], ...
    'FontWeight', 'bold', ...
    'Callback', @(~,~) sendCmd(s, 'x', status_txt, 'Logging STOPPED'));

uicontrol(fig, 'Style', 'pushbutton', 'String', 'Clear Log', ...
    'Position', [760 5 100 28], ...
    'Callback', @(~,~) sendCmd(s, 'r', status_txt, 'Log cleared'));

lp_btn = uicontrol(fig, 'Style', 'pushbutton', 'String', 'Low Power: OFF', ...
    'Position', [870 5 110 28], ...
    'BackgroundColor', [0.2 0.8 0.2], ...
    'FontWeight', 'bold', ...
    'Callback', @(src,~) toggleLowPower(s, src, status_txt));

% ── Session tracking ──────────────────────────────────────────
colors   = {[0 0.45 0.74], [0.85 0.33 0.10], [0.47 0.67 0.19], ...
            [0.49 0.18 0.56], [0.30 0.75 0.93]};
last_ts  = 0;
t_offset = 0;
session  = 1;
h = animatedline(ax_plot, 'Color', colors{1}, 'LineWidth', 1.2);
last_packet_time = tic;

% ── Read loop ─────────────────────────────────────────────────
while ishandle(fig)
    if ~ishandle(fig), break; end
    try
        line = readline(s);
        if ~ishandle(fig), break; end
        line = strtrim(line);

        if isempty(line)
            continue
        end

        % Status lines — show in status bar
        if line(1) == '#'
            status_txt.String = ['DV: ' strtrim(line(2:end))];
            status_txt.ForegroundColor = [0 0 0];
            continue
        end

        % Parse CSV — now 13 fields
        vals = str2double(strsplit(line, ','));
        if numel(vals) ~= 14 || any(isnan(vals(1:9)))
            continue
        end

        ts_ms     = vals(1);
        az        = vals(4);
        battery_v = vals(13);

        % Detect timer reset — start a new colored line
        if ts_ms < last_ts
            t_offset = t_offset + (last_ts / 1000);
            session  = mod(session, length(colors)) + 1;
            h = animatedline(ax_plot, 'Color', colors{session}, 'LineWidth', 1.2);
            disp(['Session ' num2str(session) ' started (timer reset detected)'])
        end
        last_ts = ts_ms;
        status_txt.ForegroundColor = [0 0 0];

        ts_s = (ts_ms / 1000) + t_offset;

        fprintf(fid, '%s\n', line);

        addpoints(h, ts_s, az);

        % ── Update battery display ─────────────────────────────
        pct = (battery_v - BATT_EMPTY) / (BATT_FULL - BATT_EMPTY);
        pct = max(0, min(1, pct));
        bar_max_h = 156;
        bar_h     = round(pct * bar_max_h);

        batt_bar.Position(4) = bar_h;

        if pct > 0.5
            batt_bar.BackgroundColor = [0.2 0.8 0.2];
        elseif pct > 0.2
            batt_bar.BackgroundColor = [0.9 0.8 0.0];
        else
            batt_bar.BackgroundColor = [0.9 0.2 0.2];
        end

        batt_pct_txt.String = sprintf('Battery: %d%%', round(pct * 100));
        batt_v_txt.String   = sprintf('%.2f V', battery_v);

        drawnow limitrate;
        last_packet_time = tic;

    catch ME
        if ~ishandle(fig), break; end
        if ~contains(ME.message, 'Operation terminated by user')
            disp(ME.message)
        end
    end

   % flagging connection timeout
   if ishandle(fig) && toc(last_packet_time) > 2.0
       status_txt.String = 'Status: WARNING - connection lost';
       status_txt.ForegroundColor = [0.9 0 0];
   end
end

% ── Cleanup ───────────────────────────────────────────────────
fclose(fid);
clear s;
disp(['Done. Saved to: ' LOG_FILE])

% ── Post-run plot ─────────────────────────────────────────────
data = readmatrix(LOG_FILE);
if size(data, 1) > 1
    t = data(:,1) / 1000;
    figure('Name', 'Drop — Full Run');
    plot(t, data(:,2), 'r', t, data(:,3), 'g', t, data(:,4), 'b', 'LineWidth', 1);
    legend('ax','ay','az'); xlabel('Time (s)'); ylabel('g');
    title('Accelerometer — Full Run'); grid on;
end

% ── helper function ──
function sendCmd(s, ch, txt, msg)
    write(s, ch, 'char');
    txt.String = ['Status: ' msg];
end
% ── low-power mode function ──
function toggleLowPower(s, btn, txt)
    if strcmp(btn.String, 'Low Power: OFF')
        write(s, 'p', 'char');
        btn.String = 'Low Power: ON';
        btn.BackgroundColor = [0.9 0.2 0.2];
        txt.String = 'Status: Low power mode ON';
    else
        write(s, 'p', 'char');
        btn.String = 'Low Power: OFF';
        btn.BackgroundColor = [0.2 0.8 0.2];
        txt.String = 'Status: Low power mode OFF';
    end
end