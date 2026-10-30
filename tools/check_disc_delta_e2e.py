"""End-to-end check of the [[disc_delta]] mod operation on a real disc.

Drives `mod_runtime_test --disc-delta-e2e`, which stages a format-6 package
holding the given BPS, resolves and commits it through the runtime exactly as
a launch does (source-hash guard, BPS apply with CRC checks, target-hash
check, sector cache), then reads every sector back through the ISO wrapper
the CD controller uses and hashes the stream. It runs twice: the first commit
applies the patch and writes the cache, the second must be served from it.

    python tools/check_disc_delta_e2e.py \
        --test-exe runtime/build-agent-disc/mod_runtime_test.exe \
        --disc "../disc/Digimon World (USA).bin" \
        --bps ../../dw15-release/unpacked/digimon_world_1_5_test1.bps \
        --target-sha256 c51099d7b2cf44401eca5a54f7838f85205bccfff207b72dc2de7675ec6b8826

--source-sha256 defaults to the disc's own SHA-256. Exit status is 0 only when
both passes serve exactly the target image.
"""
import argparse
import hashlib
import subprocess
import sys
from pathlib import Path


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--test-exe", required=True, type=Path,
                    help="built mod_runtime_test executable")
    ap.add_argument("--disc", required=True, type=Path,
                    help="stock raw .bin (or .cue/.chd) the delta targets")
    ap.add_argument("--bps", required=True, type=Path, help="BPS patch")
    ap.add_argument("--source-sha256", help="expected stock image SHA-256")
    ap.add_argument("--target-sha256", required=True,
                    help="SHA-256 of the patched raw image")
    args = ap.parse_args()

    source = args.source_sha256 or sha256_file(args.disc)
    cmd = [str(args.test_exe), "--disc-delta-e2e", str(args.disc),
           str(args.bps), source.lower(), args.target_sha256.lower()]
    print("running:", " ".join(cmd), flush=True)
    result = subprocess.run(cmd)
    print("PASS" if result.returncode == 0 else "FAIL")
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
