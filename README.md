# dodc

The program tries to factor composites from https://mklasson.com/factors/ using factoring tools like `gmp-ecm`, `msieve`, and `cado-nfs`. You'll need to build and/or install those yourself on your system.


## Supported platforms

- macOS: Builds and runs fine on Sonoma with Apple Silicon.
- Linux: Untested. Probably quite easy to get up and running.
- Windows: Untested. Used to work back in 2013. Probably doable.


## Quick install

1. `git clone https://github.com/emklasson/dodc.git`
2. `cd dodc`
3. `make`
4. Read and edit `dodc.cfg` to your liking.
5. `./dodc`
6. Kick back and enjoy!

You can override any setting from the configuration file by passing it on the command line, e.g.:

`./dodc -nmax 800 -b1 11e6 -workers 8`.


## Supported factoring tools

You'll need at least one of these to make any use of dodc.

### ECM, P-1, P+1

- gmp-ecm: https://members.loria.fr/PZimmermann/records/ecm/devel.html

### QS

- msieve: https://sourceforge.net/projects/msieve/
- Unfortunately yafu doesn't build on Apple Silicon. https://github.com/bbuhrow/yafu

### GNFS, SNFS

- cado-nfs: https://github.com/cado-nfs/cado-nfs
  - Customise `dodc_cado_nfs.cfg` for your system.
  - `dodc_cado_nfs.py` is run by dodc, but can be run separately as well. For SNFS it makes a simple poly file and then uses one of cado-nfs' included parameter files with slight tweaking. I'm sure this is not optimal in any way, but works well enough for numbers around 600 bits at least.
