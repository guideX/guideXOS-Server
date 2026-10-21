import argparse
import json
import pathlib
import subprocess

p = argparse.ArgumentParser()
p.add_argument("--config", required=True)
p.add_argument("--repo", required=True)
p.add_argument("--log", required=True)
p.add_argument("--exit-code", required=True)
a = p.parse_args()

config = json.loads(pathlib.Path(a.config).read_text(encoding="utf-8"))
command = [
    config["qacCliPath"],
    "run",
    "--project",
    config["project"],
    "--mode",
    config["qacMode"],
    "--",
    config["qemuPath"],
    *config["arguments"],
]
with pathlib.Path(a.log).open("w", encoding="utf-8", newline="\n") as log:
    result = subprocess.run(command, cwd=a.repo, stdout=log, stderr=subprocess.STDOUT, check=False)
pathlib.Path(a.exit_code).write_text(f"{result.returncode}\n", encoding="ascii")
print(f"QAC CLI exited with code {result.returncode}; log={a.log}")
raise SystemExit(result.returncode)
