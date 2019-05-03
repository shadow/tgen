'''
  tgentools
  Authored by Rob Jansen, 2015
  See LICENSE for licensing information
'''

import matplotlib
matplotlib.use('Agg') # for systems without X11
import matplotlib.pyplot as pyplot
from matplotlib.backends.backend_pdf import PdfPages

import numpy, time, logging, re
from abc import abstractmethod, ABCMeta

class Visualization(object):

    __metaclass__ = ABCMeta

    def __init__(self, hostpatterns):
        self.datasets = []
        self.hostpatterns = hostpatterns

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
            self.__plot_byte_timeseries("time_to_first_byte_recv")
            logging.info("Plotting last byte for all transfers")
            self.__plot_lastbyte_all()
            logging.info("Plotting median last byte per client")
            self.__plot_lastbyte_median()
            logging.info("Plotting median last byte per client")
            self.__plot_lastbyte_mean()
            logging.info("Plotting median last byte per client")
            self.__plot_lastbyte_max()
            logging.info("Plotting last byte time series")
            self.__plot_byte_timeseries("time_to_last_byte_recv")
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

    def __get_nodes(self, anal):
        nodes = set()
        for client in anal.get_nodes():
            if len(self.hostpatterns) > 0:
                for pattern in self.hostpatterns:
                    if re.search(pattern, client) is not None:
                        nodes.add(client)
            else:
                nodes.add(client)
        return nodes

    def __plot_firstbyte(self):
        f = None

        for (anal, label, lineformat) in self.datasets:
            fb = []
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "time_to_first_byte_recv" in d:
                    for b in d["time_to_first_byte_recv"]:
                        if f is None: f = pyplot.figure()
                        for sec in d["time_to_first_byte_recv"][b]: fb.extend(d["time_to_first_byte_recv"][b][sec])
            if f is not None and len(fb) > 0:
                x, y = getcdf(fb)
                pyplot.plot(x, y, lineformat, label=label)

        if f is not None:
            pyplot.xlabel("Download Time (s)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("time to download first byte, all clients")
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_lastbyte_all(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "time_to_last_byte_recv" in d:
                    for b in d["time_to_last_byte_recv"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pyplot.figure()
                        if bytes not in lb: lb[bytes] = []
                        for sec in d["time_to_last_byte_recv"][b]: lb[bytes].extend(d["time_to_last_byte_recv"][b][sec])
            for bytes in lb:
                x, y = getcdf(lb[bytes])
                pyplot.figure(figs[bytes].number)
                pyplot.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pyplot.figure(figs[bytes].number)
            pyplot.xlabel("Download Time (s)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("time to download {0} bytes, all downloads".format(bytes))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_lastbyte_median(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "time_to_last_byte_recv" in d:
                    for b in d["time_to_last_byte_recv"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pyplot.figure()
                        if bytes not in lb: lb[bytes] = []
                        client_lb_list = []
                        for sec in d["time_to_last_byte_recv"][b]: client_lb_list.extend(d["time_to_last_byte_recv"][b][sec])
                        lb[bytes].append(numpy.median(client_lb_list))
            for bytes in lb:
                x, y = getcdf(lb[bytes])
                pyplot.figure(figs[bytes].number)
                pyplot.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pyplot.figure(figs[bytes].number)
            pyplot.xlabel("Download Time (s)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("median time to download {0} bytes, each client".format(bytes))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_lastbyte_mean(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "time_to_last_byte_recv" in d:
                    for b in d["time_to_last_byte_recv"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pyplot.figure()
                        if bytes not in lb: lb[bytes] = []
                        client_lb_list = []
                        for sec in d["time_to_last_byte_recv"][b]: client_lb_list.extend(d["time_to_last_byte_recv"][b][sec])
                        lb[bytes].append(numpy.mean(client_lb_list))
            for bytes in lb:
                x, y = getcdf(lb[bytes])
                pyplot.figure(figs[bytes].number)
                pyplot.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pyplot.figure(figs[bytes].number)
            pyplot.xlabel("Download Time (s)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("mean time to download {0} bytes, each client".format(bytes))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_lastbyte_max(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "time_to_last_byte_recv" in d:
                    for b in d["time_to_last_byte_recv"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pyplot.figure()
                        if bytes not in lb: lb[bytes] = []
                        client_lb_list = []
                        for sec in d["time_to_last_byte_recv"][b]: client_lb_list.extend(d["time_to_last_byte_recv"][b][sec])
                        lb[bytes].append(numpy.max(client_lb_list))
            for bytes in lb:
                x, y = getcdf(lb[bytes])
                pyplot.figure(figs[bytes].number)
                pyplot.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pyplot.figure(figs[bytes].number)
            pyplot.xlabel("Download Time (s)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("max time to download {0} bytes, each client".format(bytes))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_byte_timeseries(self, bytekey="time_to_last_byte_recv"):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            lb = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if bytekey in d:
                    for b in d[bytekey]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pyplot.figure()
                        if bytes not in lb: lb[bytes] = {}
                        for secstr in d[bytekey][b]:
                            sec = int(secstr)
                            if sec not in lb[bytes]: lb[bytes][sec] = []
                            lb[bytes][sec].extend(d[bytekey][b][secstr])
            for bytes in lb:
                pyplot.figure(figs[bytes].number)
                x = [sec for sec in lb[bytes]]
                x.sort()
                y = [numpy.mean(lb[bytes][sec]) for sec in x]
                if len(y) > 20:
                    y = movingaverage(y, len(y)*0.05)
                start_sec = min(x)
                x[:] = [sec - start_sec for sec in x]
                pyplot.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pyplot.figure(figs[bytes].number)
            pyplot.xlabel("Tick (s)")
            pyplot.ylabel("Download Time (s)")
            pyplot.title("moving avg. time to download {0} of {1} bytes, all clients over time".format('first' if 'first' in bytekey else 'last', bytes))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_downloads(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            dls = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "time_to_last_byte_recv" in d:
                    for b in d["time_to_last_byte_recv"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pyplot.figure()
                        if bytes not in dls: dls[bytes] = {}
                        if client not in dls[bytes]: dls[bytes][client] = 0
                        for sec in d["time_to_last_byte_recv"][b]: dls[bytes][client] += len(d["time_to_last_byte_recv"][b][sec])
            for bytes in dls:
                x, y = getcdf(dls[bytes].values(), shownpercentile=1.0)
                pyplot.figure(figs[bytes].number)
                pyplot.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pyplot.figure(figs[bytes].number)
            pyplot.xlabel("Downloads Completed (num)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("number of {0} byte downloads completed, each client".format(bytes))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_downloads_timeseries(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            dls = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "time_to_last_byte_recv" in d:
                    for b in d["time_to_last_byte_recv"]:
                        bytes = int(b)
                        if bytes not in figs: figs[bytes] = pyplot.figure()
                        if bytes not in dls: dls[bytes] = {}
                        for secstr in d["time_to_last_byte_recv"][b]:
                            sec = int(secstr)
                            if sec not in dls[bytes]: dls[bytes][sec] = 0
                            dls[bytes][sec] += len(d["time_to_last_byte_recv"][b][secstr])
            for bytes in dls:
                pyplot.figure(figs[bytes].number)
                x = [sec for sec in dls[bytes]]
                x.sort()
                y = [dls[bytes][sec] for sec in x]
                if len(y) > 20:
                    y = movingaverage(y, len(y)*0.05)
                start_sec = min(x)
                x[:] = [sec - start_sec for sec in x]
                pyplot.plot(x, y, lineformat, label=label)

        for bytes in sorted(figs.keys()):
            pyplot.figure(figs[bytes].number)
            pyplot.xlabel("Tick (s)")
            pyplot.ylabel("Downloads Completed (num)")
            pyplot.title("moving avg. num. {0} byte downloads completed, all clients over time".format(bytes))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_errors(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            codes_observed = set()
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        codes_observed.add(code)

            dls = {}
            for code in codes_observed:
                figs[code] = pyplot.figure()
                dls[code] = {}

            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in codes_observed:
                        if client not in dls[code]:
                            dls[code][client] = 0
                        if code in d["errors"]:
                            for sec in d["errors"][code]:
                                dls[code][client] += len(d["errors"][code][sec])
            for code in dls:
                x, y = getcdf([dls[code][client] for client in dls[code]], shownpercentile=1.0)
                pyplot.figure(figs[code].number)
                pyplot.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pyplot.figure(figs[code].number)
            pyplot.xlabel("Download Errors (num)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("number of transfer {0} errors, each client".format(code))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_errors_timeseries(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            dls = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pyplot.figure()
                        if code not in dls: dls[code] = {}
                        for secstr in d["errors"][code]:
                            sec = int(secstr)
                            if sec not in dls[code]: dls[code][sec] = 0
                            dls[code][sec] += len(d["errors"][code][secstr])
            for code in dls:
                pyplot.figure(figs[code].number)
                start_sec, end_sec = min(dls[code]), max(dls[code])
                x = range(start_sec, end_sec)
                y = []
                for sec in x:
                    if sec in dls[code]:
                        y.append(dls[code][sec])
                    else:
                        y.append(0)
                if len(y) > 20:
                    y = movingaverage(y, len(y)*0.05)
                x = [sec - start_sec for sec in x]
                pyplot.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pyplot.figure(figs[code].number)
            pyplot.xlabel("Tick (s)")
            pyplot.ylabel("Download Errors (num)")
            pyplot.title("moving avg. num. transfer {0} errors, all clients over time".format(code))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_errsizes_all(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            err = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pyplot.figure()
                        if code not in err: err[code] = []
                        client_err_list = []
                        for sec in d["errors"][code]: client_err_list.extend(d["errors"][code][sec])
                        for b in client_err_list: err[code].append(int(b[0]) / 1024.0)
            for code in err:
                x, y = getcdf(err[code])
                pyplot.figure(figs[code].number)
                pyplot.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pyplot.figure(figs[code].number)
            pyplot.xlabel("Data Transferred (KiB)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("bytes transferred before {0} error, all downloads".format(code))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_errsizes_median(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            err = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pyplot.figure()
                        if code not in err: err[code] = []
                        client_err_list = []
                        for sec in d["errors"][code]: client_err_list.extend(d["errors"][code][sec][0])
                        err[code].append(numpy.median(client_err_list) / 1024.0)
            for code in err:
                x, y = getcdf(err[code])
                pyplot.figure(figs[code].number)
                pyplot.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pyplot.figure(figs[code].number)
            pyplot.xlabel("Data Transferred (KiB)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("median bytes transferred before {0} error, each client".format(code))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

    def __plot_errsizes_mean(self):
        figs = {}

        for (anal, label, lineformat) in self.datasets:
            err = {}
            for client in self.__get_nodes(anal):
                d = anal.get_tgen_stream_summary(client)
                if d is None: continue
                if "errors" in d:
                    for code in d["errors"]:
                        if code not in figs: figs[code] = pyplot.figure()
                        if code not in err: err[code] = []
                        client_err_list = []
                        for sec in d["errors"][code]: client_err_list.extend(d["errors"][code][sec][0])
                        err[code].append(numpy.mean(client_err_list) / 1024.0)
            for code in err:
                x, y = getcdf(err[code])
                pyplot.figure(figs[code].number)
                pyplot.plot(x, y, lineformat, label=label)

        for code in sorted(figs.keys()):
            pyplot.figure(figs[code].number)
            pyplot.xlabel("Data Transferred (KiB)")
            pyplot.ylabel("Cumulative Fraction")
            pyplot.title("mean bytes transferred before {0} error, each client".format(code))
            pyplot.legend(loc="best")
            pyplot.tight_layout(pad=0.3)
            self.page.savefig()
            pyplot.close()

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
