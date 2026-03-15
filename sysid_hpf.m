% IMU Low Pass Filter Script
% Reads imu_log.csv, applies a low pass filter to accelerometer and
% gyroscope data, plots original vs filtered, and saves filtered data

clc; clear; close all;

%% ── Parameters ──────────────────────────────────────────────────────────────
cutoff_hz   = 2;    % cutoff frequency in Hz — lower = smoother, adjust as needed
filter_order = 3;   % Butterworth filter order — higher = sharper cutoff

%% ── Load Data ───────────────────────────────────────────────────────────────
data = readtable('imu_log_SELFTEST.csv');

% Parse timestamps and compute sample rate
timestamps = datetime(data.timestamp, 'InputFormat', 'yyyy-MM-dd HH:mm:ss.SSSSSS');
dt = seconds(diff(timestamps));
fs = 1 / mean(dt);
fprintf('Detected sample rate: %.1f Hz\n', fs);

% Extract signals
ax = data.ax;  ay = data.ay;  az = data.az;
gx = data.gx;  gy = data.gy;  gz = data.gz;
t = seconds(timestamps - timestamps(1));   % time vector derived directly from timestamps

%% ── Design Butterworth Low Pass Filter ──────────────────────────────────────
nyquist = fs / 2;
Wn      = cutoff_hz / nyquist;   % normalized cutoff frequency
[b, a]  = butter(filter_order, Wn, 'low');

%% ── Apply Filter ─────────────────────────────────────────────────────────────
% filtfilt = zero phase filtering (no time delay introduced)
ax_f = filtfilt(b, a, ax);  ay_f = filtfilt(b, a, ay);  az_f = filtfilt(b, a, az);
gx_f = filtfilt(b, a, gx);  gy_f = filtfilt(b, a, gy);  gz_f = filtfilt(b, a, gz);

%% ── Plot Accelerometer ───────────────────────────────────────────────────────
figure('Name', 'Accelerometer');
titles = {'Accel X (m/s²)', 'Accel Y (m/s²)', 'Accel Z (m/s²)'};
raw_a  = {ax, ay, az};
filt_a = {ax_f, ay_f, az_f};

for i = 1:3
    subplot(3, 1, i);
    plot(t, raw_a{i},  'Color', [0.7 0.7 0.7], 'DisplayName', 'Raw');
    hold on;
    plot(t, filt_a{i}, 'b', 'LineWidth', 1.5, 'DisplayName', 'Filtered');
    title(titles{i});
    xlabel('Time (s)'); ylabel('m/s²');
    legend; grid on;
end

%% ── Plot Gyroscope ───────────────────────────────────────────────────────────
figure('Name', 'Gyroscope');
titles = {'Gyro X (rps)', 'Gyro Y (rps)', 'Gyro Z (rps)'};
raw_g  = {gx, gy, gz};
filt_g = {gx_f, gy_f, gz_f};

for i = 1:3
    subplot(3, 1, i);
    plot(t, raw_g{i},  'Color', [0.7 0.7 0.7], 'DisplayName', 'Raw');
    hold on;
    plot(t, filt_g{i}, 'r', 'LineWidth', 1.5, 'DisplayName', 'Filtered');
    title(titles{i});
    xlabel('Time (s)'); ylabel('rps');
    legend; grid on;
end

% %% ── Save Filtered Data to CSV ────────────────────────────────────────────────
% filtered = table(data.timestamp, ax_f, ay_f, az_f, gx_f, gy_f, gz_f, ...
%     'VariableNames', {'timestamp','ax','ay','az','gx','gy','gz'});
% 
% writetable(filtered, 'imuonly_trial3_filtered.csv');
% fprintf('Filtered data saved!\n');