'''
  tgentools
  Authored by Rob Jansen, 2015
  See LICENSE for licensing information
'''

import matplotlib
matplotlib.use('Agg') # for systems without X11
import matplotlib.pyplot as pyplot

import numpy, re, sys

def main():
    interpacket_to_server = []
    interpacket_to_client = []

    with open("packet.log", 'r') as inf:
        for line in inf:
            if re.search("next origin-bound delay is", line):
                delay = int(line.strip().split()[19])
                interpacket_to_client.append(delay)
            elif re.search("next server-bound delay is", line):
                delay = int(line.strip().split()[19])
                interpacket_to_server.append(delay)

    pyplot.figure()

    x, y = getcdf(interpacket_to_server)
    pyplot.plot(x, y, label="to server")

    x, y = getcdf(interpacket_to_client)
    pyplot.plot(x, y, label="to client")

    pyplot.xscale("log")
    pyplot.grid(axis='both')

    pyplot.title("Tor packet markov model measured with PrivCount")
    pyplot.xlabel("Inter-packet delay (usec)")
    pyplot.ylabel("Cumulative Fraction")
    pyplot.legend(loc="best")
    pyplot.tight_layout(pad=0.3)
    pyplot.savefig("inter-packet-delays.pdf")

# helper - compute the window_size moving average over the data in interval
def movingaverage(interval, window_size):
    window = numpy.ones(int(window_size)) / float(window_size)
    return numpy.convolve(interval, window, 'same')

# # helper - cumulative fraction for y axis
def cf(d): return numpy.arange(1.0, float(len(d)) + 1.0) / float(len(d))

# # helper - return step-based CDF x and y values
# # only show to the 99th percentile by default
def getcdf(data, shownpercentile=0.99, maxpoints=10000.0):
    data = sorted(data)
    frac = cf(data)
    k = len(data) / maxpoints
    x, y, lasty = [], [], 0.0
    for i in iter(range(int(round(len(data) * shownpercentile)))):
        if i % k > 1.0: continue
        assert not numpy.isnan(data[i])
        x.append(data[i])
        y.append(lasty)
        x.append(data[i])
        y.append(frac[i])
        lasty = frac[i]
    return x, y

if __name__ == '__main__': sys.exit(main())
