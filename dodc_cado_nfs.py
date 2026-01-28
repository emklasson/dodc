#!/usr/bin/env python3

import datetime
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def write_poly(filename, k, a, n, d, expression, cfg):
    with open(filename, "w") as f:
        f.write(f"# Polynomial for {expression}\n")
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
        f.write("skew: 1.0\n")
        f.write(f"c5: {k_poly}\n")
        f.write(f"c0: {d_poly}\n")
        f.write("Y1: 1\n")
        f.write(f"Y0: {-root}\n")


def write_parameters(param_filename, poly_filename: Path, dodc_cado_path: Path, k, a, n, d, expression, cfg):
    threads = cfg['tasks.threads']
    with open(param_filename, "w") as f:
        f.write(f"# {expression}\n")
        f.write(f"name = {poly_filename.stem}\n")
        f.write(f"N = {cfg['cofactor']}\n\n")
        f.write(f"tasks.threads = {threads}\n")
        f.write(f"tasks.sieve.las.threads = {threads}\n")
        f.write('''
# The remaining values in this file are from the sample parameters for F9 = 2^512+1
# They seem to work reasonably well for k*2^n+-1 with n around 450-500 though.

tasks.lim0 = 2300000
tasks.lim1 = 1200000
tasks.lpb0 = 26
tasks.lpb1 = 26

# We supply the SNFS polynomial, so we don't want any polynomial selection
# to happen. To this effect we set admin and admax both to 0.
tasks.polyselect.admin = 0
tasks.polyselect.admax = 0
tasks.polyselect.adrange = 0
''')
        f.write(f"tasks.polyselect.import = {dodc_cado_path/poly_filename}\n")
        f.write('''
# Sieving parameters
tasks.sieve.mfb0 = 52
tasks.sieve.mfb1 = 52
#tasks.sieve.lambda0 = 2.1  # Better to let cado choose
#tasks.sieve.lambda1 = 2.2  # Better to let cado choose
tasks.I = 12
tasks.qmin = 2000000
tasks.sieve.qrange = 5000
tasks.sieve.sqside = 0

# linear algebra
tasks.linalg.m = 64
tasks.linalg.n = 64
tasks.linalg.characters.nchar = 50
''')


def write_script(filename: Path, param_filename: Path, dodc_cado_path: Path, cfg, gnfs, number):
    log_path = dodc_cado_path/filename.with_suffix(".log")
    with open(filename, "w") as f:
        f.write("#!/bin/sh\n\n")
        f.write(f"cd {cfg['cado_nfs_path']}\n")
        f.write("source cado-nfs.venv/bin/activate\n")

        # cado-nfs is using stderr to write everything except the factors.
        redirect_stderr = f"2>> {log_path}"

        if gnfs:
            arg = f"-t {cfg['tasks.threads']} {number}"
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
        print(f"Couldn't parse expression: '{expression}'.")
        sys.exit(1)


def main(expression, threads, gnfs, cofactor):
    expression = expression.strip()
    if gnfs and all(c.isdecimal() for c in expression):
        number = expression
        filename_stem = Path(f"c{len(number)}_{number}")
    else:
        [k, a, n, d] = parse_expression(expression)
        number = k * a ** n + d
        filename_stem = Path(f"{k}t{a}r{n}{'p' if d > 0 else 'm'}{abs(d)}")

    poly_filename = filename_stem.with_suffix(".poly")
    param_filename = filename_stem.with_suffix(".param")
    sh_filename = filename_stem.with_suffix(".sh")
    log_filename = filename_stem.with_suffix(".log")

    try:
        with open("dodc_cado_nfs.cfg", "r") as f:
            cfg = json.load(f)
    except json.JSONDecodeError:
        print("Error decoding cfg file. Exiting...")
        sys.exit(1)

    if threads:
        cfg['tasks.threads'] = threads

    cfg['cofactor'] = cofactor if cofactor != 0 else number

    dir = "cado_nfs"
    os.makedirs(dir, exist_ok=True)
    os.chdir(dir)
    dodc_cado_path = Path(os.getcwd())
    if not gnfs:
        write_poly(poly_filename, k, a, n, d, expression, cfg)
        write_parameters(param_filename, poly_filename, dodc_cado_path, k, a, n, d, expression, cfg)
    write_script(sh_filename, param_filename, dodc_cado_path, cfg, gnfs, number)

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
        print(f"ERROR: no factors found. Check {dodc_cado_path/log_filename}.")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python dodc_cado_nfs.py <expression> [-t <threads>] [-c cofactor] [-g]")
        print("  <expression>\tAn expression on the form <k*a^n+d>, <k*a^n-d>, or an integer.")
        print("    E.g. 17*2^453+1")
        print("  -t <threads>\tThe max number of threads to use.")
        print("  -c <cofactor>\tThe composite to factor for SNFS.")
        print("  -g\tUse GNFS.")
        sys.exit(1)

    threads = None
    gnfs = False
    cofactor = 0
    i = 1
    while i < len(sys.argv):
        if sys.argv[i] == "-t" and i + 1 < len(sys.argv):
            threads = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == "-c" and i + 1 < len(sys.argv):
            cofactor = int(sys.argv[i + 1])
            i += 2
        elif sys.argv[i] == "-g":
            gnfs = True
            i += 1
        else:
            expression = sys.argv[i]
            i += 1

    main(expression, threads, gnfs, cofactor)
