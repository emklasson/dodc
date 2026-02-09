#!/usr/bin/env python3

import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def write_poly(filename, k, a, n, d, cfg):
    with open(filename, "w") as f:
        f.write(f"# Polynomial for {cfg['expression']}\n")
        k_poly = k
        d_poly = d
        n_poly = n
        n_mod = n % 5
        if n_mod < 3:
            k_poly = k * a ** n_mod
            n_poly = n - n_mod
        else:
            d_poly = d * a ** (5 - n_mod)
            n_poly = n + 5 - n_mod
        f.write(f"# adjusted: {k_poly} * {a}^{n_poly} {'+' if d_poly > 0 else '-'} {abs(d_poly)}\n")
        root = a**int(n_poly / 5)
        f.write(f"# root {a}^{int(n_poly / 5)} = {root}\n")
        f.write(f"# poly x - {root}\n")
        f.write(f"n: {cfg['cofactor']}\n")
        f.write(f"skew: {(d_poly / k_poly) ** (1 / 5)}\n")
        f.write(f"c5: {k_poly}\n")
        f.write(f"c0: {d_poly}\n")
        f.write("Y1: 1\n")
        f.write(f"Y0: {-root}\n")


def write_cado_params(f_out, param_dir: Path, cfg):
    target_digits = float(cfg['sd'])
    best_fit = None
    best_filename: Path = None
    for f in param_dir.iterdir():
        if f.is_file() and f.name.startswith("params.c"):
            d = int(f.name.split(".c")[1])
            if best_fit is None or (abs(d - target_digits) < best_fit):
                best_fit = abs(d - target_digits)
                best_filename = f

    if best_filename is None:
        print(f"Error: Couldn't find a cado-nfs parameter file suitable for {target_digits} digits.")
        sys.exit(1)

    skip_prefixes = ["#","name =", "tasks.polyselect", "tasks.sieve.lambda"]
    replace_prefixes = ["tasks.lim0", "tasks.lim1", "tasks.lpb0", "tasks.lpb1",
                        "tasks.sieve.mfb0", "tasks.sieve.mfb1",
                        "tasks.sieve.ncurves0", "tasks.sieve.ncurves1"]
    f_out.write(f"# SNFS digits: {len(str(cfg['number']))}. Target digits: {target_digits}. Using cado-nfs file {best_filename.name} as base.\n\n")
    with open(best_filename, "r") as f_in:
        for line in f_in.readlines():
            if line.strip() == "" or any(line.startswith(prefix) for prefix in skip_prefixes):
                continue
            for prefix in replace_prefixes:
                if line.startswith(prefix):
                    line = f"{prefix[:-1]}{int(prefix[-1]) ^ 1}{line[len(prefix):]}"
                    break
            f_out.write(line)


def write_parameters(param_filename, poly_filename: Path, dodc_cado_path: Path, cfg):
    threads = cfg['tasks.threads']
    with open(param_filename, "w") as f:
        f.write(f"# {cfg['expression']}\n")
        f.write(f"name = {poly_filename.stem}\n")
        f.write(f"N = {cfg['cofactor']}\n\n")
        f.write(f"tasks.threads = {threads}\n")
        f.write(f"tasks.sieve.las.threads = {threads}\n")
        f.write(f"tasks.polyselect.import = {dodc_cado_path/poly_filename}\n\n")

        write_cado_params(f, Path(cfg['cado_nfs_path'])/"parameters/factor/", cfg)

        f.write('''
# We supply the SNFS polynomial, so we don't want any polynomial selection
# to happen. To this effect we set admin and admax both to 0.
tasks.polyselect.admin = 0
tasks.polyselect.admax = 0
tasks.polyselect.adrange = 0

tasks.sieve.sqside = 0
''')


def write_script(filename: Path, param_filename: Path, dodc_cado_path: Path, cfg):
    log_path = dodc_cado_path/filename.with_suffix(".log")
    with open(filename, "w") as f:
        f.write("#!/bin/sh\n\n")
        f.write(f"cd {cfg['cado_nfs_path']}\n")
        f.write("source cado-nfs.venv/bin/activate\n")

        # cado-nfs is using stderr to write everything except the factors.
        redirect_stderr = f"2>> {log_path}"

        if cfg['gnfs']:
            arg = f"-t {cfg['tasks.threads']} {cfg['number']}"
        else:
            arg = dodc_cado_path/param_filename

        # Something bugs out in cado-nfs when called from dodc unless server.ssl=no.
        f.write(f"nice -n {cfg['priority']} ./cado-nfs.py {arg} server.ssl=no slaves.hostnames=localhost {redirect_stderr}\n")

        # f.write(f"nice -n {cfg['priority']} ./cado-nfs.py {17*2**208+1} {redirect_stderr}\n")


def parse_time(seconds):
    return str(datetime.timedelta(seconds=int(float(seconds))))


def parse_expression(expression):
    try:
        [k, a, n, d] = [int(t) for t in re.split(r'[*^+-]', expression)]
        if '-' in expression:
            d = -d
        return [k, a, n, d]
    except ValueError:
        print(f"Error: Couldn't parse expression: '{expression}'.")
        sys.exit(1)


def main(cfg):
    if cfg['gnfs'] and all(c.isdecimal() for c in cfg['expression']):
        number = cfg['expression']
        filename_stem = Path(f"c{len(number)}_{number}")
    else:
        [k, a, n, d] = parse_expression(cfg['expression'])
        number = k * a ** n + d
        filename_stem = Path(f"{k}t{a}r{n}{'p' if d > 0 else 'm'}{abs(d)}")

    poly_filename = filename_stem.with_suffix(".poly")
    param_filename = filename_stem.with_suffix(".param")
    sh_filename = filename_stem.with_suffix(".sh")
    log_filename = filename_stem.with_suffix(".log")

    if cfg['cofactor'] == 0:
        cfg['cofactor'] = number

    if 'sd' not in cfg:
        cfg['sd'] = int(len(str(number)) * cfg['sf'])

    cfg['number'] = number

    dir = "cado_nfs"
    os.makedirs(dir, exist_ok=True)
    os.chdir(dir)
    dodc_cado_path = Path(os.getcwd())
    if not cfg['gnfs']:
        write_poly(poly_filename, k, a, n, d, cfg)
        write_parameters(param_filename, poly_filename, dodc_cado_path, cfg)
    write_script(sh_filename, param_filename, dodc_cado_path, cfg)

    result = subprocess.run(["sh", f"./{sh_filename}"], capture_output=True, text=True)

    with open(log_filename, "a+") as f:
        f.write(result.stdout)
        f.seek(0, os.SEEK_SET)
        result_lines = f.readlines()

    for line in result_lines[-10:]:
        if "Complete Factorization / Discrete logarithm: Total cpu/elapsed time" in line:
            times = line.split("entire Complete Factorization ")[1]
            if len(times) > 1:
                times = re.split(r"[\/ ]", times)
                print(f"CPU time: {parse_time(times[0])}. Wall time: {parse_time(times[1])}")

    if all(c.isdecimal() or c == " " for c in result_lines[-1].strip()):
        print(f"factors: {result_lines[-1].strip()}")
    else:
        print(f"Error: No factors found. Check {dodc_cado_path/log_filename}.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python dodc_cado_nfs.py [options] <expression>")
        print("  <expression>     An expression on the form <k*a^n+d>, <k*a^n-d>, or an integer.")
        print("                   E.g. 17*2^453+1\n")
        print("  Options:")
        print("    -c <cofactor>  The composite to factor for SNFS.")
        print("    -g             Use GNFS.")
        print("    -sf <sf>       Set SNFS difficulty conversion factor. Defaults to 2/3.")
        print("    -sd <n>        Set SNFS difficulty. Determines parameters chosen. Defaults to #digits * <sf>.")
        print("                   -sf is ignored if -sd is set.")
        print("    -t <threads>   The max number of threads to use.")
        sys.exit(1)

    try:
        with open("dodc_cado_nfs.cfg", "r") as f:
            cfg = json.load(f)
    except json.JSONDecodeError:
        print("Error: Couldn't decode cfg file.")
        sys.exit(1)

    cfg['cofactor'] = 0
    cfg['gnfs'] = False
    cfg['expression'] = ""
    cfg['sf'] = 2/3
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "-t" and i + 1 < len(sys.argv):
            cfg['tasks.threads'] = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == "-c" and i + 1 < len(sys.argv):
            cfg['cofactor'] = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == "-g":
            cfg['gnfs'] = True
            i += 1
        elif sys.argv[i] == "-sd" and i + 1 < len(sys.argv):
            cfg['sd'] = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == "-sf" and i + 1 < len(sys.argv):
            cfg['sf'] = float(sys.argv[i + 1])
            i += 2
        else:
            cfg['expression'] = sys.argv[i].strip()
            i += 1

    if cfg['expression'] == "":
        print("Error: No expression provided.")
        sys.exit(1)

    main(cfg)
