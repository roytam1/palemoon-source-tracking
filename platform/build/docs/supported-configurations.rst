.. _build_supported_configurations:

========================
Supported Configurations
========================

This page attempts to document supported build configurations.

Windows
=======

We support building on Windows 7 and newer operating systems using
Visual Studio 2015.

The following are not fully supported by Mozilla (but may work):

* Building without the latest *MozillaBuild* Windows development
  environment
* Building with Mingw or any other non-Visual Studio toolchain.

Linux
=====

Linux 2.6 and later kernels are supported.

Most distributions are supported as long as the proper package
dependencies are in place. Running ``mach bootstrap`` should install
packages for popular Linux distributions. ``configure`` will typically
detect missing dependencies and inform you how to disable features to
work around unsatisfied dependencies.
