import matplotlib
matplotlib.use('Agg') # for systems without X11
import matplotlib.pyplot as pyplot

import sys
import scipy.stats as ss
import numpy

def main():
    normal()
    lognormal()
    exponential()
    pareto()

def normal():
    pyplot.figure()

    x = numpy.linspace(0, 50, 5000)

    plot_normal(x, cdf=True, mu=25, sigma=10, label="Normal")

    empirical = []
    with open("normal", 'r') as inf:
        for line in inf:
            empirical.append(int(line.strip()))
    x, y = getcdf(empirical)
    pyplot.plot(x, y, label="Empirical")

    pyplot.grid(axis='both')

    pyplot.xlabel("Value")
    pyplot.ylabel("Cumulative Fraction")
    pyplot.legend(loc="best")
    pyplot.tight_layout(pad=0.3)
    pyplot.savefig("normal-dist.pdf")

def lognormal():
    pyplot.figure()

    x = numpy.linspace(0.0, 10.0, 5000)

    plot_lognormal(x, cdf=True, label="LogNormal")

    empirical = []
    with open("lognormal", 'r') as inf:
        for line in inf:
            empirical.append(int(line.strip()))
    x, y = getcdf(empirical)
    pyplot.plot(x, y, label="Empirical")

    pyplot.grid(axis='both')

    pyplot.xlabel("Value")
    pyplot.ylabel("Cumulative Fraction")
    pyplot.legend(loc="best")
    pyplot.tight_layout(pad=0.3)
    pyplot.savefig("lognormal-dist.pdf")

def exponential():
    pyplot.figure()

    x = numpy.linspace(0.0, 5.0, 5000)

    plot_exponential(x, cdf=True, label="Exponential")

    empirical = []
    with open("exponential", 'r') as inf:
        for line in inf:
            empirical.append(int(line.strip()))
    x, y = getcdf(empirical)
    pyplot.plot(x, y, label="Empirical")

    pyplot.grid(axis='both')

    pyplot.xlabel("Value")
    pyplot.ylabel("Cumulative Fraction")
    pyplot.legend(loc="best")
    pyplot.tight_layout(pad=0.3)
    pyplot.savefig("exponential-dist.pdf")

def pareto():
    pyplot.figure()

    x = numpy.linspace(0.0, 20.0, 5000)

    plot_pareto(x, b=1, cdf=True, label="Pareto")

    empirical = []
    with open("pareto", 'r') as inf:
        for line in inf:
            empirical.append(int(line.strip()))
    x, y = getcdf(empirical)
    pyplot.plot(x, y, label="Empirical")

    pyplot.grid(axis='both')

    pyplot.xlabel("Value")
    pyplot.ylabel("Cumulative Fraction")
    pyplot.legend(loc="best")
    pyplot.tight_layout(pad=0.3)
    pyplot.savefig("pareto-dist.pdf")

def plot_exponential(x_range, mu=0, sigma=1, cdf=False, **kwargs):
    '''
    Plots the exponential distribution function for a given x range
    If mu and sigma are not provided, standard exponential is plotted
    If cdf=True cumulative distribution is plotted
    Passes any keyword arguments to matplotlib plot function
    '''
    x = x_range
    if cdf:
        y = ss.expon.cdf(x, mu, sigma)
    else:
        y = ss.expon.pdf(x, mu, sigma)
    pyplot.plot(x, y, **kwargs)

def plot_normal(x_range, mu=0, sigma=1, cdf=False, **kwargs):
    '''
    Plots the normal distribution function for a given x range
    If mu and sigma are not provided, standard normal is plotted
    If cdf=True cumulative distribution is plotted
    Passes any keyword arguments to matplotlib plot function
    '''
    x = x_range
    if cdf:
        y = ss.norm.cdf(x, mu, sigma)
    else:
        y = ss.norm.pdf(x, mu, sigma)
    pyplot.plot(x, y, **kwargs)

def plot_lognormal(x_range, mu=0, sigma=1, cdf=False, **kwargs):
    '''
    Plots the normal distribution function for a given x range
    If mu and sigma are not provided, standard normal is plotted
    If cdf=True cumulative distribution is plotted
    Passes any keyword arguments to matplotlib plot function
    '''
    x = x_range
    if cdf:
        y = ss.lognorm.cdf(x, 1, mu, sigma)
    else:
        y = ss.lognorm.pdf(x, 1, mu, sigma)
    pyplot.plot(x, y, **kwargs)

def plot_pareto(x_range, b=1, mu=0, sigma=1, cdf=False, **kwargs):
    '''
    Plots the normal distribution function for a given x range
    If mu and sigma are not provided, standard normal is plotted
    If cdf=True cumulative distribution is plotted
    Passes any keyword arguments to matplotlib plot function
    '''
    x = x_range
    if cdf:
        y = ss.pareto.cdf(x, b, mu, sigma)
    else:
        y = ss.pareto.pdf(x, b, mu, sigma)
    pyplot.plot(x, y, **kwargs)

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
