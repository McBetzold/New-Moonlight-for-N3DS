# Contribution Guide

## Got a Question or Problem?
Start with the [README](../README.md): it covers pairing, the stream views, settings, Apollo hosts, 3D and performance. The app also has guides for 3D and Wake-on-LAN on the 3DS itself.

## Found an Issue?
Please [open an issue](https://github.com/McBetzold/New-Moonlight-for-N3DS/issues) and check first:
- [ ] The bug is in New Moonlight for N3DS, not in Sunshine, Apollo or GeForce Experience.
- [ ] Your 3DS and PC are on the same network and no firewall blocks the stream.
- [ ] No other issue (open or closed) already reports it.

Say which 3DS model you have (New 3DS / New 3DS XL / New 2DS XL, or an original 3DS), the release you installed, the host and its version, and the stream settings. The debug view in the in-stream menu shows the frame rate, losses and timings; a photo of it helps with stream problems. Steps that make the problem happen are the most useful part of a report.

## Feature request
Feature requests are welcome as issues. Describe what you want to do and why; pull requests that implement it are even better.

## Submitting a Pull Request
Pull requests go against `n3ds-main`. Before submitting, check:
- [ ] Your branch is based on a recent commit and can be merged cleanly.
- [ ] Your code uses the same code style as the rest of the code.
- [ ] The GitHub Actions build of your branch succeeds.
- [ ] Your commits only change the necessary lines, and do not accidentally change whitespace or add/remove empty lines.
- [ ] You tested the change on a 3DS, and say in the pull request what you tested.
