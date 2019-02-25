'''
  tgentools
  Authored by Rob Jansen, 2015
  See LICENSE for licensing information
'''

import matplotlib; matplotlib.use('Agg')  # for systems without X11
from matplotlib.backends.backend_pdf import PdfPages
import pylab, numpy, time, logging
from abc import abstractmethod, ABCMeta

'''
pylab.rcParams.update({
    'backend': 'PDF',
    'font.size': 16,
    'figure.max_num_figures' : 50,
    'figure.figsize': (6, 4.5),
    'figure.dpi': 100.0,
    'figure.subplot.left': 0.10,
    'figure.subplot.right': 0.95,
    'figure.subplot.bottom': 0.13,
    'figure.subplot.top': 0.92,
    'grid.color': '0.1',
    'axes.grid' : True,
    'axes.titlesize' : 'small',
    'axes.labelsize' : 'small',
    'axes.formatter.limits': (-4, 4),
    'xtick.labelsize' : 'small',
    'ytick.labelsize' : 'small',
    'lines.linewidth' : 2.0,
    'lines.markeredgewidth' : 0.5,
    'lines.markersize' : 10,
    'legend.fontsize' : 'x-small',
    'legend.fancybox' : False,
    'legend.shadow' : False,
    'legend.ncol' : 1.0,
    'legend.borderaxespad' : 0.5,
    'legend.numpoints' : 1,
    'legend.handletextpad' : 0.5,
    'legend.handlelength' : 1.6,
    'legend.labelspacing' : .75,
    'legend.markerscale' : 1.0,
    'ps.useafm' : True,
    'pdf.use14corefonts' : True,
    'text.usetex' : True,
})
'''

class Visualization(object):

    __metaclass__ = ABCMeta

    def __init__(self):
        self.datasets = []

    def add_dataset(self, analysis, label, lineformat):
        self.datasets.append((analysis, label, lineformat))

    @abstractmethod
    def plot_all(self, output_prefix):
        pass

class TGenVisualization(Visualization):

    def plot_all(self, output_prefix):
        if len(self.datasets) > 0:
            prefix = output_prefix + '.' if output_prefix is not None else ''
            ts = time.strftime("%Y-%m-%d_%H:%M:%S")
            pagename = "{0}tgen.viz.{1}.pdf".format(prefix, ts)

            logging.info("Starting to plot graphs to {}".format(pagename))

            self.page = PdfPages(pagename)
            logging.info("Plotting first byte CDF")
            self.__plot_firstbyte()
            logging.info("Plotting first byte time series")
            self.__plot_byte_timeseries("time_to_first_byte")
            logging.info("Plotting last byte for all transfers")
            self.__plot_lastbyte_all()
            logging.info("Plotting median last byte per client")
            self.__plot_lastbyte_median()
            logging.info("Plotting median last byte per client")
            self.__plot_lastbyte_mean()
            logging.info("Plotting median last byte per client")
            self.__plot_lastbyte_max()
            logging.info("Plotting last byte time series")
            self.__plot_byte_timeseries("time_to_last_byte")
            logging.info("Plotting number of downloads CDF")
            self.__plot_downloads()
            logging.info("Plotting number of downloads time series")
            self.__plot_downloads_timeseries()
            logging.info("Plotting number of errors")
            self.__plot_errors()
            logging.info("Plotting number of errors time series")
            self.__plot_errors_timeseries()
            logging.info("Plotting bytes downloaded before errors for all transfers")
            self.__plot_errsizes_all()
            logging.info("Plotting median bytes downloaded before errors per client")
            self.__plot_errsizes_median()
            logging.info("Plotting mean bytes downloaded before errors per client")
            self.__plot_errsizes_mean()
            self.page.close()
            logging.info("Saved PDF page {}".format(pagename))

    def __plot_firstbyte(self):
        f = None

        for (anal, label, lineformat) in self.datasets:
            fb = []
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "time_to_first_byte" in d:
                    for b in d["time_to_first_byte"]:
                        if f is None: f = pylab.figure()
                        for sec in d["time_to_first_byte"][b]: fb.extend(d["time_to_first_byte"][b][sec])
            if f is not None and len(fb) > 0:
                x, y = getcdf(fb)
                pylab.plot(x, y, lineformat, label=label)

        if f is not None:
            pylab.xlabel("Download Time (s)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("time to download first byte, all clients")
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_lastbyte_all(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "time_to_last_byte" in d:
                    for b in d["time_to_last_byte"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pylab.figure()
                        if bytes not in lb: lb[bytes] = []
                        for sec in d["time_to_last_byte"][b]: lb[bytes].extend(d["time_to_last_byte"][b][sec])
            for bytes in lb:
                x, y = getcdf(lb[bytes])
                pylab.figure(figs[bytes].number)
                pylab.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pylab.figure(figs[bytes].number)
            pylab.xlabel("Download Time (s)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("time to download {0} bytes, all downloads".format(bytes))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_lastbyte_median(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "time_to_last_byte" in d:
                    for b in d["time_to_last_byte"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pylab.figure()
                        if bytes not in lb: lb[bytes] = []
                        client_lb_list = []
                        for sec in d["time_to_last_byte"][b]: client_lb_list.extend(d["time_to_last_byte"][b][sec])
                        lb[bytes].append(numpy.median(client_lb_list))
            for bytes in lb:
                x, y = getcdf(lb[bytes])
                pylab.figure(figs[bytes].number)
                pylab.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pylab.figure(figs[bytes].number)
            pylab.xlabel("Download Time (s)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("median time to download {0} bytes, each client".format(bytes))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_lastbyte_mean(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "time_to_last_byte" in d:
                    for b in d["time_to_last_byte"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pylab.figure()
                        if bytes not in lb: lb[bytes] = []
                        client_lb_list = []
                        for sec in d["time_to_last_byte"][b]: client_lb_list.extend(d["time_to_last_byte"][b][sec])
                        lb[bytes].append(numpy.mean(client_lb_list))
            for bytes in lb:
                x, y = getcdf(lb[bytes])
                pylab.figure(figs[bytes].number)
                pylab.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pylab.figure(figs[bytes].number)
            pylab.xlabel("Download Time (s)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("mean time to download {0} bytes, each client".format(bytes))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_lastbyte_max(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "time_to_last_byte" in d:
                    for b in d["time_to_last_byte"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pylab.figure()
                        if bytes not in lb: lb[bytes] = []
                        client_lb_list = []
                        for sec in d["time_to_last_byte"][b]: client_lb_list.extend(d["time_to_last_byte"][b][sec])
                        lb[bytes].append(numpy.max(client_lb_list))
            for bytes in lb:
                x, y = getcdf(lb[bytes])
                pylab.figure(figs[bytes].number)
                pylab.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pylab.figure(figs[bytes].number)
            pylab.xlabel("Download Time (s)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("max time to download {0} bytes, each client".format(bytes))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_byte_timeseries(self, bytekey="time_to_last_byte"):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if bytekey in d:
                    for b in d[bytekey]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pylab.figure()
                        if bytes not in lb: lb[bytes] = {}
                        for sec in d[bytekey][b]:
                            if sec not in lb[bytes]: lb[bytes][sec] = []
                            lb[bytes][sec].extend(d[bytekey][b][sec])
            for bytes in lb:
                pylab.figure(figs[bytes].number)
                x = [sec for sec in lb[bytes]]
                x.sort()
                y = [numpy.mean(lb[bytes][sec]) for sec in x]
                pylab.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pylab.figure(figs[bytes].number)
            pylab.xlabel("Tick (s)")
            pylab.ylabel("Download Time (s)")
            pylab.title("mean time to download {0} of {1} bytes, all clients over time".format('first' if 'first' in bytekey else 'last', bytes))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_downloads(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            dls = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "time_to_last_byte" in d:
                    for b in d["time_to_last_byte"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pylab.figure()
                        if bytes not in dls: dls[bytes] = {}
                        if client not in dls[bytes]: dls[bytes][client] = 0
                        for sec in d["time_to_last_byte"][b]: dls[bytes][client] += len(d["time_to_last_byte"][b][sec])
            for bytes in dls:
                x, y = getcdf(dls[bytes].values(), shownpercentile=1.0)
                pylab.figure(figs[bytes].number)
                pylab.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pylab.figure(figs[bytes].number)
            pylab.xlabel("Downloads Completed (num)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("number of {0} byte downloads completed, each client".format(bytes))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_downloads_timeseries(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            dls = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "time_to_last_byte" in d:
                    for b in d["time_to_last_byte"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pylab.figure()
                        if bytes not in dls: dls[bytes] = {}
                        for sec in d["time_to_last_byte"][b]:
                            if sec not in dls[bytes]: dls[bytes][sec] = 0
                            dls[bytes][sec] += len(d["time_to_last_byte"][b][sec])
            for bytes in dls:
                pylab.figure(figs[bytes].number)
                x = [sec for sec in dls[bytes]]
                x.sort()
                y = [dls[bytes][sec] for sec in x]
                pylab.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pylab.figure(figs[bytes].number)
            pylab.xlabel("Tick (s)")
            pylab.ylabel("Downloads Completed (num)")
            pylab.title("number of {0} byte downloads completed, all clients over time".format(bytes))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_errors(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            dls = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pylab.figure()
                        if code not in dls: dls[code] = {}
                        if client not in dls[code]: dls[code][client] = 0
                        for sec in d["errors"][code]: dls[code][client] += len(d["errors"][code][sec])
            for code in dls:
                x, y = getcdf([dls[code][client] for client in dls[code]], shownpercentile=1.0)
                pylab.figure(figs[code].number)
                pylab.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pylab.figure(figs[code].number)
            pylab.xlabel("Download Errors (num)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("number of transfer {0} errors, each client".format(code))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_errors_timeseries(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            dls = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pylab.figure()
                        if code not in dls: dls[code] = {}
                        for sec in d["errors"][code]:
                            if sec not in dls[code]: dls[code][sec] = 0
                            dls[code][sec] += len(d["errors"][code][sec])
            for code in dls:
                pylab.figure(figs[code].number)
                x = [sec for sec in dls[code]]
                x.sort()
                y = [dls[code][sec] for sec in x]
                pylab.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pylab.figure(figs[code].number)
            pylab.xlabel("Tick (s)")
            pylab.ylabel("Download Errors (num)")
            pylab.title("number of transfer {0} errors, all clients over time".format(code))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_errsizes_all(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            err = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pylab.figure()
                        if code not in err: err[code] = []
                        client_err_list = []
                        for sec in d["errors"][code]: client_err_list.extend(d["errors"][code][sec])
                        for b in client_err_list: err[code].append(int(b) / 1024.0)
            for code in err:
                x, y = getcdf(err[code])
                pylab.figure(figs[code].number)
                pylab.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pylab.figure(figs[code].number)
            pylab.xlabel("Data Transferred (KiB)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("bytes transferred before {0} error, all downloads".format(code))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_errsizes_median(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            err = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pylab.figure()
                        if code not in err: err[code] = []
                        client_err_list = []
                        for sec in d["errors"][code]: client_err_list.extend(d["errors"][code][sec])
                        err[code].append(numpy.median(client_err_list) / 1024.0)
            for code in err:
                x, y = getcdf(err[code])
                pylab.figure(figs[code].number)
                pylab.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pylab.figure(figs[code].number)
            pylab.xlabel("Data Transferred (KiB)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("median bytes transferred before {0} error, each client".format(code))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

    def __plot_errsizes_mean(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            err = {}
            for client in anal.get_nodes():
                d = anal.get_tgen_transfers_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pylab.figure()
                        if code not in err: err[code] = []
                        client_err_list = []
                        for sec in d["errors"][code]: client_err_list.extend(d["errors"][code][sec])
                        err[code].append(numpy.mean(client_err_list) / 1024.0)
            for code in err:
                x, y = getcdf(err[code])
                pylab.figure(figs[code].number)
                pylab.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pylab.figure(figs[code].number)
            pylab.xlabel("Data Transferred (KiB)")
            pylab.ylabel("Cumulative Fraction")
            pylab.title("mean bytes transferred before {0} error, each client".format(code))
            pylab.legend(loc="lower right")
            self.page.savefig()
            pylab.close()

# helper - compute the window_size moving average over the data in interval
def movingaverage(interval, window_size):
    window = numpy.ones(int(window_size)) / float(window_size)
    return numpy.convolve(interval, window, 'same')

# # helper - cumulative fraction for y axis
def cf(d): return pylab.arange(1.0, float(len(d)) + 1.0) / float(len(d))

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
