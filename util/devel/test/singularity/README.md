# Where to find images

* You can use singularity search to look for containers
* You can find docker containers on https://hub.docker.com

# Notes on writing .def files

## Example using a singularity library image
BootStrap: library
From: ubuntu:16.04

## Example using a docker image
BootStrap: docker
From: ubuntu:16.04



%post commands run as root
%runscript commands run as regular user

# Commands to run

## to construct the image
singularity build --fakeroot singularity.sif singularity.def

#To create an image for experimenting with where you can install
# more packages etc, use this:
# singularity build --sandbox --fakeroot singularity.sif singularity.def


## to run the runscript
singularity run singularity.sif

## to run a shell within the image
singularity shell singularity.sif

By default, environment is included in run commands.
Also the directory stays the same (so e.g. a chapel directory
will appear next to singularity.sif if it is created from
within the image)
