"""
Android 示例构建脚本
用法: python build_android_example.py <示例名称> [--log <日志文件>]
"""

import argparse
import configparser
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path


def run_cmd(cmd, cwd=None, log_file=None, env=None):
    print(f"  > {cmd}")
    result = subprocess.run(
        cmd, cwd=cwd, shell=True, capture_output=True, text=True, env=env,
    )
    if log_file:
        log_file.write(f"$ {cmd}\n{result.stdout}")
        if result.stderr:
            log_file.write(result.stderr)
        log_file.write(f"exit_code={result.returncode}\n\n")
    if result.returncode != 0:
        print(f"  [FAIL] exit_code={result.returncode}")
        if result.stderr.strip():
            print(result.stderr.strip()[-500:])
    return result.returncode


def detect_ndk_version(sdk_dir):
    ndk_dir = Path(sdk_dir) / "ndk"
    if not ndk_dir.is_dir():
        return None
    versions = sorted(
        [d.name for d in ndk_dir.iterdir() if d.is_dir()],
        key=lambda v: [int(x) if x.isdigit() else 0 for x in re.split(r'[.\-]', v)],
    )
    return versions[-1] if versions else None


def sync_local_properties(android_dir, sdk_dir):
    lp = Path(android_dir) / "local.properties"
    sdk_line = f"sdk.dir={str(sdk_dir).replace(chr(92), '/')}"
    sdk_line = sdk_line.replace(":", "\\:")
    if lp.exists():
        content = lp.read_text(encoding="utf-8")
        if sdk_line.replace("\\:", ":") in content.replace("\\:", ":"):
            return
    lp.write_text(sdk_line + "\n", encoding="utf-8")
    print(f"  写入 {lp}")


def sync_ndk_version(android_dir, ndk_version):
    bg = Path(android_dir) / "build.gradle"
    content = bg.read_text(encoding="utf-8")
    pattern = r'ndkVersion\s*=\s*"[^"]*"'
    replacement = f'ndkVersion = "{ndk_version}"'
    if re.search(pattern, content):
        new_content = re.sub(pattern, replacement, content)
        if new_content != content:
            bg.write_text(new_content, encoding="utf-8")
            print(f"  ndkVersion -> {ndk_version}")


def main():
    parser = argparse.ArgumentParser(description="构建 Android 示例 APK")
    parser.add_argument("example", help="示例名称 (如 JoltPhysics_rmlui)")
    parser.add_argument("--log", default=None, help="调试日志输出文件路径")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    ini_path = script_dir / "build_android_example.ini"
    template_path = script_dir / "build_android_example.ini.template"
    if not ini_path.exists():
        if template_path.exists():
            shutil.copy(str(template_path), str(ini_path))
            print(f"[INFO] 已从模板创建 {ini_path.name}，请修改后重新运行")
        else:
            print(f"[ERROR] 找不到 {ini_path} 和模板文件")
        sys.exit(1)

    config = configparser.ConfigParser()
    config.read(str(ini_path), encoding="utf-8")

    java_home = config["env"]["JAVA_HOME"]
    sdk_dir = config["env"]["SDK_DIR"]
    gradle_args = config["env"].get("GRADLE_ARGS", "--no-parallel")
    android_dir = script_dir / "android"
    build_env = {**os.environ, "JAVA_HOME": java_home}

    # 自动检测 NDK
    ndk_version = detect_ndk_version(sdk_dir)
    if not ndk_version:
        print(f"[ERROR] SDK 目录中未找到 NDK: {sdk_dir}/ndk/")
        sys.exit(1)

    example = args.example
    apk_path = android_dir / "examples" / "bin" / f"{example}-release.apk"
    example_dir = android_dir / "examples" / example
    assets_overlay = example_dir / "assets" / "overlay"
    cxx_dir = example_dir / ".cxx"

    print("=" * 60)
    print(f"  Android 示例构建: {example}")
    print(f"  JAVA_HOME:  {java_home}")
    print(f"  SDK_DIR:    {sdk_dir}")
    print(f"  NDK:        {ndk_version}")
    print(f"  项目目录:   {android_dir}")
    print("=" * 60)

    log_file = None
    if args.log:
        log_path = Path(args.log)
        log_path.parent.mkdir(parents=True, exist_ok=True)
        log_file = open(args.log, "w", encoding="utf-8")
        print(f"  日志文件:   {args.log}")

    # 同步 SDK/NDK 配置
    print("\n[sync]")
    sync_local_properties(str(android_dir), sdk_dir)
    sync_ndk_version(str(android_dir), ndk_version)

    # 清理缓存
    print("\n[clean]")
    if apk_path.exists():
        apk_path.unlink()
        print(f"  删除旧 APK")
    if assets_overlay.exists():
        shutil.rmtree(assets_overlay)
        print(f"  清理 assets/overlay")
    if cxx_dir.exists():
        shutil.rmtree(cxx_dir)
        print(f"  清理 .cxx 缓存")

    # 构建
    print("\n[build]")
    build_cmd = f"gradlew.bat :{example}:assembleRelease {gradle_args}"
    t0 = time.time()
    exit_code = run_cmd(build_cmd, cwd=str(android_dir), log_file=log_file, env=build_env)
    elapsed = time.time() - t0

    print(f"\n{'=' * 60}")
    if exit_code == 0:
        print(f"  BUILD SUCCESSFUL ({elapsed:.1f}s)")
        if apk_path.exists():
            size_mb = apk_path.stat().st_size / (1024 * 1024)
            print(f"  APK: {apk_path} ({size_mb:.1f} MB)")
    else:
        print(f"  BUILD FAILED ({elapsed:.1f}s, exit_code={exit_code})")
        if log_file:
            print(f"  查看日志: {args.log}")
    print("=" * 60)

    if log_file:
        log_file.close()
    sys.exit(exit_code)


if __name__ == "__main__":
    main()
