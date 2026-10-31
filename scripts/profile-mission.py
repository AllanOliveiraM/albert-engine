#!/usr/bin/env python3
"""Run a bounded Linux mission profile with live thermal monitoring and automatic shutdown."""
import argparse
import csv
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import signal
import statistics
import subprocess
import threading
import time

ROOT = Path(__file__).resolve().parent.parent


def cpu_temperatures():
    values = []
    for directory in Path('/sys/class/hwmon').glob('hwmon*'):
        try:
            if directory.joinpath('name').read_text().strip() not in ('coretemp', 'k10temp', 'zenpower'):
                continue
            for sensor in directory.glob('temp*_input'):
                values.append(float(sensor.read_text()) / 1000)
        except (OSError, ValueError):
            continue
    return values


def memory_available():
    for line in Path('/proc/meminfo').read_text().splitlines():
        if line.startswith('MemAvailable:'):
            return int(line.split()[1]) / 1024
    raise RuntimeError('MemAvailable unavailable')


def process_stats(process, previous, now):
    if process is None or process.poll() is not None:
        return {}, previous
    try:
        directory = Path('/proc') / str(process.pid)
        fields = directory.joinpath('stat').read_text().rsplit(')', 1)[1].split()
        ticks = int(fields[11]) + int(fields[12])
        cpu = (ticks - previous[1]) / os.sysconf('SC_CLK_TCK') / (now - previous[0]) * 100 if previous else 0
        stats = {'cpu_pct': round(cpu, 2), 'rss_mib': int(fields[21]) * os.sysconf('SC_PAGE_SIZE') / 1048576,
                 'minor_faults': int(fields[7]), 'major_faults': int(fields[9])}
        io = dict(line.split(': ', 1) for line in directory.joinpath('io').read_text().splitlines())
        stats.update(read_mib=int(io['read_bytes']) / 1048576, write_mib=int(io['write_bytes']) / 1048576)
        return stats, (now, ticks)
    except (OSError, ValueError, IndexError):
        return {}, previous


def gpu_stats():
    if not shutil.which('nvidia-smi'):
        return {}
    fields = ['temperature.gpu', 'utilization.gpu', 'utilization.memory', 'memory.used',
              'clocks.current.graphics', 'power.draw']
    try:
        result = subprocess.run(['nvidia-smi', '--query-gpu=' + ','.join(fields), '--format=csv,noheader,nounits'],
                                capture_output=True, text=True, timeout=3, check=True)
        values = result.stdout.splitlines()[0].split(',')
        names = ['temp_c', 'util_pct', 'memory_util_pct', 'memory_mib', 'clock_mhz', 'power_w']
        return {name: float(value.strip()) for name, value in zip(names, values) if 'N/A' not in value}
    except (subprocess.SubprocessError, ValueError, IndexError):
        return {}


def power_profile():
    try:
        return subprocess.check_output(['powerprofilesctl', 'get'], text=True, timeout=3).strip()
    except (OSError, subprocess.SubprocessError):
        return 'unavailable'


def throttle_counts():
    result = {}
    for path in Path('/sys/devices/system/cpu').glob('cpu*/thermal_throttle/*throttle_count'):
        try:
            result[str(path.relative_to('/sys/devices/system/cpu'))] = int(path.read_text())
        except (OSError, ValueError):
            pass
    return result


def stop(process):
    if process is None or process.poll() is not None:
        return
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


class DutyCycle:
    """Optional duty cycle for one owned process group."""
    def __init__(self, process, fraction):
        self.process = process
        self.done = threading.Event()
        self.thread = None
        if fraction < 1:
            self.thread = threading.Thread(target=self.run, args=(fraction,), daemon=True)
            self.thread.start()

    def run(self, fraction):
        try:
            while not self.done.is_set() and self.process.poll() is None:
                os.killpg(self.process.pid, signal.SIGCONT)
                if self.done.wait(.02 * fraction):
                    break
                os.killpg(self.process.pid, signal.SIGSTOP)
                self.done.wait(.02 * (1-fraction))
        except ProcessLookupError:
            pass

    def close(self):
        self.done.set()
        if self.thread:
            self.thread.join()
        if self.process.poll() is None:
            try:
                os.killpg(self.process.pid, signal.SIGCONT)
            except ProcessLookupError:
                pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=['idle', 'pad', 'mission'], default='pad')
    parser.add_argument('--seconds', type=int, default=45)
    parser.add_argument('--quality', choices=['low', 'high'], default='low')
    parser.add_argument('--scale', type=float, default=.6)
    parser.add_argument('--fps', type=int, choices=[30, 60], default=30)
    parser.add_argument('--size', default='1280x720')
    parser.add_argument('--ui-scale', type=float, default=1)
    parser.add_argument('--effects', action='store_true', help='Enable volumetric clouds and ocean waves')
    parser.add_argument('--loading-duty', type=float, default=1, help='Optional CPU duty cycle during loading only (.1–1)')
    parser.add_argument('--runtime-duty', type=float, default=1, help='Optional duty cycle during the measured phase (.1–1); affects cadence')
    parser.add_argument('--cpu-stop', type=float, default=82)
    parser.add_argument('--gpu-stop', type=float, default=78)
    parser.add_argument('--rss-stop', type=int, default=4096, help='Application RSS ceiling in MiB')
    parser.add_argument('--port', type=int, default=50091)
    parser.add_argument('--cache-dir', type=Path, default=ROOT / 'build/simulation-cache')
    parser.add_argument('--executable', type=Path, default=ROOT / 'build/linux/bin/AlbertEngine')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not Path('/proc').is_dir() or not cpu_temperatures():
        parser.error('This monitor requires Linux and readable CPU temperature sensors')
    if not 1 <= args.seconds <= 600 or not .25 <= args.scale <= 1 or not .8 <= args.ui_scale <= 1.6 or not 1 <= args.port <= 65535:
        parser.error('Use 1–600 seconds, render scale .25–1, UI scale .8–1.6, and a valid port')
    if args.cpu_stop <= 0 or args.gpu_stop <= 0 or args.rss_stop <= 0:
        parser.error('Monitoring limits must be positive')
    if not .1 <= args.loading_duty <= 1 or not .1 <= args.runtime_duty <= 1:
        parser.error('Duty cycles must be .1–1')
    gpu_expected = shutil.which('nvidia-smi') is not None
    if gpu_expected and 'temp_c' not in gpu_stats():
        parser.error('NVIDIA temperature is unavailable; no workload was started')
    out = (args.output or ROOT / 'build/profiles' / (datetime.now().strftime('%Y%m%d-%H%M%S') + '-' + args.mode)).resolve()
    out.mkdir(parents=True, exist_ok=False)
    data = out / 'data'
    data.mkdir()
    if args.cache_dir.is_dir():
        for directory in args.cache_dir.iterdir():
            if directory.is_dir():
                data.joinpath(directory.name).symlink_to(directory.resolve(), target_is_directory=True)
    data.joinpath('settings.json').write_text(json.dumps({
        'quality': int(args.quality == 'high'), 'frame_cap': args.fps, 'antialias': False, 'vsync': True,
        'volumetric_clouds': args.effects, 'ocean_waves': args.effects, 'broadcast_panels': True,
        'launch_cloud_quality': 1, 'interface_scale': args.ui_scale,
    }))
    env = dict(os.environ, ALBERT_DATA_DIR=str(data), ALBERT_ASSET_DIR=str(ROOT / 'assets'),
               ALBERT_PACK_PATH=str(ROOT / 'build/linux/imagery/visuals-v2.pak'), ALBERT_WINDOW=args.size,
               ALBERT_MAXIMIZED='0', ALBERT_AUDIO='0', ALBERT_RENDER_SCALE=str(args.scale),
               ALBERT_FRAME_STATS='1', ALBERT_GPU_TIMING='1', ALBERT_UPLOAD_STATS='1', ALBERT_LOAD_STATS='1')
    app = client = limiter = client_limiter = None
    rows = []
    reason = 'completed'
    app_log = out.joinpath('app.log').open('w')
    client_log = out.joinpath('client.log').open('w')
    started = time.monotonic()
    initial_throttles = throttle_counts()
    initial_power_profile = power_profile()
    ready_at = running_at = None
    app_previous = client_previous = None
    last_print = -10
    print(f'Profile: {out}', flush=True)
    def interrupt(_signum, _frame):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, interrupt)
    try:
        app = subprocess.Popen([str(args.executable.resolve()), f'--rpc-port={args.port}'],
                               cwd=ROOT, env=env, stdout=app_log, stderr=app_log, start_new_session=True)
        limiter = DutyCycle(app, args.loading_duty)
        with out.joinpath('samples.csv').open('w', newline='') as file:
            writer = None
            while True:
                now = time.monotonic()
                temperatures = cpu_temperatures()
                if not temperatures:
                    reason = 'CPU temperature sensor lost'
                    break
                gpu = gpu_stats()
                if gpu_expected and 'temp_c' not in gpu:
                    reason = 'GPU temperature sensor lost'
                    break
                app_stats, app_previous = process_stats(app, app_previous, now)
                client_stats, client_previous = process_stats(client, client_previous, now)
                available = memory_available()
                log = out.joinpath('app.log').read_text(errors='replace')
                if ready_at is None and f'gRPC: 127.0.0.1:{args.port}' in log:
                    ready_at = now
                if ready_at is not None and running_at is None and now - ready_at >= 8:
                    limiter.close()
                    limiter = DutyCycle(app, args.runtime_duty)
                    running_at = now
                    if args.mode != 'idle':
                        command = [str(ROOT / 'build/linux/src/rpc/albert_flight_client'), f'127.0.0.1:{args.port}']
                        command += ['--pad'] if args.mode == 'pad' else ['--starship', '--landings=4']
                        client = subprocess.Popen(command, cwd=ROOT, env=env, stdout=client_log,
                                                  stderr=client_log, start_new_session=True)
                        client_limiter = DutyCycle(client, args.runtime_duty)
                phase = 'running' if running_at else 'warmup' if ready_at else 'loading'
                row = dict(elapsed_s=round(now-started, 2), phase=phase, cpu_temp_c=max(temperatures), available_mib=available)
                for prefix, stats in [('app', app_stats), ('client', client_stats), ('gpu', gpu)]:
                    for name, value in stats.items():
                        row[prefix + '_' + name] = value
                # Fixed columns also retain missing GPU sensors as empty, rather than fabricated zeros.
                if writer is None:
                    columns = ['elapsed_s', 'phase', 'cpu_temp_c', 'available_mib']
                    columns += [prefix + '_' + name for prefix in ('app', 'client') for name in
                                ('cpu_pct', 'rss_mib', 'minor_faults', 'major_faults', 'read_mib', 'write_mib')]
                    columns += ['gpu_' + name for name in ('temp_c', 'util_pct', 'memory_util_pct', 'memory_mib', 'clock_mhz', 'power_w')]
                    writer = csv.DictWriter(file, fieldnames=columns)
                    writer.writeheader()
                writer.writerow(row)
                file.flush()
                rows.append(row)
                if now-last_print >= 5:
                    print(f'{phase:7} CPU {row["cpu_temp_c"]:.0f} C / {app_stats.get("cpu_pct", 0):.0f}%  '
                          f'GPU {gpu.get("temp_c", "?")} C / {gpu.get("util_pct", "?")}%  '
                          f'RSS {app_stats.get("rss_mib", 0):.0f} MiB', flush=True)
                    last_print = now
                if max(temperatures) >= args.cpu_stop:
                    reason = 'CPU temperature limit'
                    break
                if gpu.get('temp_c', 0) >= args.gpu_stop:
                    reason = 'GPU temperature limit'
                    break
                if available < 1024 or app_stats.get('rss_mib', 0) > args.rss_stop:
                    reason = 'Memory limit'
                    break
                if app.poll() is not None:
                    reason = f'Application exited ({app.returncode})'
                    break
                if client is not None and client.poll() not in (None, 0):
                    reason = f'Client exited ({client.returncode})'
                    break
                if running_at and now-running_at >= args.seconds:
                    break
                if ready_at is None and now-started >= 240:
                    reason = 'Startup timeout'
                    break
                time.sleep(max(.1, 1-(time.monotonic()-now)))
    except KeyboardInterrupt:
        reason = 'Interrupted'
    finally:
        if limiter is not None:
            limiter.close()
        if client_limiter is not None:
            client_limiter.close()
        stop(client)
        stop(app)
        app_log.close()
        client_log.close()
    summary = {'reason': reason, 'arguments': {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
               'samples': len(rows), 'running_samples': sum(row['phase'] == 'running' for row in rows),
               'scripted_ui_review': bool(os.environ.get('ALBERT_REVIEW_SEQUENCE')),
               'power_profile': initial_power_profile,
               'thermal_throttle_increments': {key: value-initial_throttles[key] for key, value in throttle_counts().items()
                                               if key in initial_throttles and value > initial_throttles[key]},
               'note': 'GPU utilization and memory are device-wide; CPU percent uses one core as 100%.'}
    for field in ('cpu_temp_c', 'gpu_temp_c'):
        summary['peak_' + field] = max((row[field] for row in rows if field in row), default=None)
    for field in ('cpu_temp_c', 'gpu_temp_c', 'app_cpu_pct', 'client_cpu_pct', 'app_rss_mib', 'gpu_memory_mib'):
        values = [row[field] for row in rows if row['phase'] == 'running' and field in row]
        summary[field] = {'max': max(values), 'median': statistics.median(values)} if values else None
    out.joinpath('summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(f'Stopped: {reason}. Results: {out}', flush=True)
    return 0 if reason == 'completed' else 2


if __name__ == '__main__':
    raise SystemExit(main())
