import subprocess
import os

def generate_header():
    git_hash = subprocess.check_output(["git", "rev-parse", "HEAD"]).strip()
    with open("../lib/git_hash/git_hash.h", "w") as f:
        f.write(f'#define FIRMWARE_COMMIT_HASH "{git_hash.decode()}"\n')

generate_header()
