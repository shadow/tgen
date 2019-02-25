#!/usr/bin/env python

from distutils.core import setup

setup(name='TGenTools',
      version="0.1.0",
      description='A utility to analyze and visualize TGen output',
      author='Rob Jansen',
      url='https://github.com/shadow/tgen/',
      packages=['tgentools'],
      scripts=['tgentools/tgentools'],
     )
