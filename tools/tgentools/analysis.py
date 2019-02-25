'''
  tgentools
  Authored by Rob Jansen, 2015
  See LICENSE for licensing information
'''

import sys, os, re, json, datetime, logging

from multiprocessing import Pool, cpu_count
from signal import signal, SIGINT, SIG_IGN
from socket import gethostname
from abc import ABCMeta, abstractmethod

# tgentools imports
import tgentools.util as util

class Analysis(object):

    def __init__(self, nickname=None, ip_address=None):
        self.nickname = nickname
        self.measurement_ip = ip_address
        self.hostname = gethostname().split('.')[0]
        self.json_db = {'type':'tgen', 'version':1.0, 'data':{}}
        self.tgen_filepaths = []
        self.date_filter = None
        self.did_analysis = False

    def add_tgen_file(self, filepath):
        self.tgen_filepaths.append(filepath)

    def get_nodes(self):
        return self.json_db['data'].keys()

    def get_tgen_transfers_summary(self, node):
        try:
            return self.json_db['data'][node]['tgen']['transfers_summary']
        except:
            return None

    def analyze(self, do_simple=True, date_filter=None):
        if self.did_analysis:
            return

        self.date_filter = date_filter
        tgen_parser = TGenParser(date_filter=self.date_filter)

        for (filepaths, parser, json_db_key) in [(self.tgen_filepaths, tgen_parser, 'tgen')]:
            if len(filepaths) > 0:
                for filepath in filepaths:
                    logging.info("parsing log file at {0}".format(filepath))
                    parser.parse(util.DataSource(filepath), do_simple=do_simple)

                if self.nickname is None:
                    parsed_name = parser.get_name()
                    if parsed_name is not None:
                        self.nickname = parsed_name
                    elif self.hostname is not None:
                        self.nickname = self.hostname
                    else:
                        self.nickname = "unknown"

                if self.measurement_ip is None:
                    self.measurement_ip = "unknown"

                self.json_db['data'].setdefault(self.nickname, {'measurement_ip': self.measurement_ip}).setdefault(json_db_key, parser.get_data())

        self.did_analysis = True

    def merge(self, analysis):
        for nickname in analysis.json_db['data']:
            if nickname in self.json_db['data']:
                raise Exception("Merge does not yet support multiple Analysis objects from the same node \
                (add multiple files from the same node to the same Analysis object before calling analyze instead)")
            else:
                self.json_db['data'][nickname] = analysis.json_db['data'][nickname]

    def save(self, filename=None, output_prefix=os.getcwd(), do_compress=True, version=1.0):
        if filename is None:
            if self.date_filter is None:
                filename = "tgen.analysis.json.xz"
            else:
                filename = "{}.tgen.analysis.json.xz".format(util.date_to_string(self.date_filter))

        filepath = os.path.abspath(os.path.expanduser("{0}/{1}".format(output_prefix, filename)))
        if not os.path.exists(output_prefix):
            os.makedirs(output_prefix)

        logging.info("saving analysis results to {0}".format(filepath))

        outf = util.FileWritable(filepath, do_compress=do_compress)
        json.dump(self.json_db, outf, sort_keys=True, separators=(',', ': '), indent=2)
        outf.close()

        logging.info("done!")

    @classmethod
    def load(cls, filename="tgen.analysis.json.xz", input_prefix=os.getcwd(), version=1.0):
        filepath = os.path.abspath(os.path.expanduser("{0}".format(filename)))
        if not os.path.exists(filepath):
            filepath = os.path.abspath(os.path.expanduser("{0}/{1}".format(input_prefix, filename)))
            if not os.path.exists(filepath):
                logging.warning("file does not exist at '{0}'".format(filepath))
                return None

        logging.info("loading analysis results from {0}".format(filepath))

        inf = util.DataSource(filepath)
        inf.open()
        db = json.load(inf.get_file_handle())
        inf.close()

        logging.info("done!")

        if 'type' not in db or 'version' not in db:
            logging.warning("'type' or 'version' not present in database")
            return None
        elif db['type'] != 'tgen' or db['version'] != 1.0:
            logging.warning("type or version not supported (type={0}, version={1})".format(db['type'], db['version']))
            return None
        else:
            analysis_instance = cls()
            analysis_instance.json_db = db
            return analysis_instance

class SerialAnalysis(Analysis):

    def analyze(self, paths, do_simple=True, date_filter=None):
        logging.info("processing input from {0} paths...".format(len(paths)))

        analyses = []
        for path in paths:
            a = Analysis()
            a.add_tgen_file(path)
            a.analyze(do_simple=do_simple, date_filter=date_filter)
            analyses.append(a)

        logging.info("merging {0} analysis results now...".format(len(analyses)))
        while analyses is not None and len(analyses) > 0:
            self.merge(analyses.pop())
        logging.info("done merging results: {0} total nicknames present in json db".format(len(self.json_db['data'])))

def subproc_analyze_func(analysis_args):
    signal(SIGINT, SIG_IGN)  # ignore interrupts
    a = analysis_args[0]
    do_simple = analysis_args[1]
    date_filter = analysis_args[2]
    a.analyze(do_simple=do_simple, date_filter=date_filter)
    return a

class ParallelAnalysis(Analysis):

    def analyze(self, paths, do_simple=True, date_filter=None,
        num_subprocs=cpu_count()):
        logging.info("processing input from {0} paths...".format(len(paths)))

        analysis_jobs = []
        for path in paths:
            a = Analysis()
            a.add_tgen_file(path)
            analysis_args = [a, do_simple, date_filter]
            analysis_jobs.append(analysis_args)

        analyses = None
        pool = Pool(num_subprocs if num_subprocs > 0 else cpu_count())
        try:
            mr = pool.map_async(subproc_analyze_func, analysis_jobs)
            pool.close()
            while not mr.ready(): mr.wait(1)
            analyses = mr.get()
        except KeyboardInterrupt:
            logging.info("interrupted, terminating process pool")
            pool.terminate()
            pool.join()
            sys.exit()

        logging.info("merging {0} analysis results now...".format(len(analyses)))
        while analyses is not None and len(analyses) > 0:
            self.merge(analyses.pop())
        logging.info("done merging results: {0} total nicknames present in json db".format(len(self.json_db['data'])))

class TransferStatusEvent(object):

    def __init__(self, line):
        self.is_success = False
        self.is_error = False
        self.is_complete = False

        parts = line.strip().split()
        self.unix_ts_end = util.timestamp_to_seconds(parts[2])

        transport_parts = parts[8].split(',')
        self.endpoint_local = transport_parts[2]
        self.endpoint_proxy = transport_parts[3]
        self.endpoint_remote = transport_parts[4]

        transfer_parts = parts[10].split(',')

        # for id, combine the time with the transfer num; this is unique for each node,
        # as long as the node was running tgen without restarting for 100 seconds or longer
        # #self.transfer_id = "{0}-{1}".format(round(self.unix_ts_end, -2), transfer_num)
        self.transfer_id = "{0}:{1}".format(transfer_parts[0], transfer_parts[1])  # id:count

        self.hostname_local = transfer_parts[2]
        self.method = transfer_parts[3]  # 'GET' or 'PUT'
        self.filesize_bytes = int(transfer_parts[4])
        self.hostname_remote = transfer_parts[5]
        self.error_code = transfer_parts[8].split('=')[1]

        self.total_bytes_read = int(parts[11].split('=')[1])
        self.total_bytes_write = int(parts[12].split('=')[1])

        # the commander is the side that sent the command,
        # i.e., the side that is driving the download, i.e., the client side
        progress_parts = parts[13].split('=')
        self.is_commander = (self.method == 'GET' and 'read' in progress_parts[0]) or \
                            (self.method == 'PUT' and 'write' in progress_parts[0])
        self.payload_bytes_status = int(progress_parts[1].split('/')[0])

        self.unconsumed_parts = None if len(parts) < 16 else parts[15:]
        self.elapsed_seconds = {}

class TransferCompleteEvent(TransferStatusEvent):
    def __init__(self, line):
        super(TransferCompleteEvent, self).__init__(line)
        self.is_complete = True

        i = 0
        elapsed_seconds = 0.0
        # match up self.unconsumed_parts[0:11] with the events in the transfer_steps enum
        for k in ['socket_create', 'socket_connect', 'proxy_init', 'proxy_choice', 'proxy_request',
                  'proxy_response', 'command', 'response', 'first_byte', 'last_byte', 'checksum']:
            # parse out the elapsed time value
            keyval = self.unconsumed_parts[i]
            i += 1

            val = float(int(keyval.split('=')[1]))
            if val >= 0.0:
                elapsed_seconds = val / 1000000.0  # usecs to secs
                self.elapsed_seconds.setdefault(k, elapsed_seconds)

        self.unix_ts_start = self.unix_ts_end - elapsed_seconds
        del(self.unconsumed_parts)

class TransferSuccessEvent(TransferCompleteEvent):
    def __init__(self, line):
        super(TransferSuccessEvent, self).__init__(line)
        self.is_success = True

class TransferErrorEvent(TransferCompleteEvent):
    def __init__(self, line):
        super(TransferErrorEvent, self).__init__(line)
        self.is_error = True

class Transfer(object):
    def __init__(self, tid):
        self.id = tid
        self.last_event = None
        self.payload_progress = {decile:None for decile in [0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]}

    def add_event(self, status_event):
        progress_frac = float(status_event.payload_bytes_status) / float(status_event.filesize_bytes)
        for decile in sorted(self.payload_progress.keys()):
            if progress_frac >= decile and self.payload_progress[decile] is None:
                self.payload_progress[decile] = status_event.unix_ts_end
        self.last_event = status_event

    def get_data(self):
        e = self.last_event
        if e is None or not e.is_complete:
            return None
        d = e.__dict__
        d['elapsed_seconds']['payload_progress'] = {decile: self.payload_progress[decile] - e.unix_ts_start for decile in self.payload_progress if self.payload_progress[decile] is not None}
        return d

class Parser(object):
    __metaclass__ = ABCMeta
    @abstractmethod
    def parse(self, source, do_simple):
        pass
    @abstractmethod
    def get_data(self):
        pass
    @abstractmethod
    def get_name(self):
        pass

class TGenParser(Parser):

    def __init__(self, date_filter=None):
        ''' date_filter should be given in UTC '''
        self.state = {}
        self.transfers = {}
        self.transfers_summary = {'time_to_first_byte':{}, 'time_to_last_byte':{}, 'errors':{}}
        self.name = None
        self.date_filter = date_filter

    def __is_date_valid(self, date_to_check):
        if self.date_filter is None:
            # we are not asked to filter, so every date is valid
            return True
        else:
            # we are asked to filter, so the line is only valid if the date matches the filter
            # both the filter and the unix timestamp should be in UTC at this point
            return util.do_dates_match(self.date_filter, date_to_check)

    def __parse_line(self, line, do_simple):
        if self.name is None and re.search("Initializing traffic generator", line) is not None:
            self.name = line.strip().split()[12]

        if self.date_filter is not None:
            parts = line.split(' ', 3)
            if len(parts) < 4: # the 3rd is the timestamp, the 4th is the rest of the line
                return True
            unix_ts = float(parts[2])
            line_date = datetime.datetime.utcfromtimestamp(unix_ts).date()
            if not self.__is_date_valid(line_date):
                return True

        if not do_simple and re.search("state\sRESPONSE\sto\sstate\sPAYLOAD", line) is not None:
            # another run of tgen starts the id over counting up from 1
            # if a prev transfer with the same id did not complete, we can be sure it never will
            parts = line.strip().split()
            transfer_parts = parts[7].strip().split(',')
            transfer_id = "{0}:{1}".format(transfer_parts[0], transfer_parts[1])  # id:count
            if transfer_id in self.state:
                self.state.pop(transfer_id)

        elif not do_simple and re.search("transfer-status", line) is not None:
            status = TransferStatusEvent(line)
            xfer = self.state.setdefault(status.transfer_id, Transfer(status.transfer_id))
            xfer.add_event(status)

        elif re.search("transfer-complete", line) is not None:
            complete = TransferSuccessEvent(line)

            if not do_simple:
                xfer = self.state.setdefault(complete.transfer_id, Transfer(complete.transfer_id))
                xfer.add_event(complete)
                self.transfers[xfer.id] = xfer.get_data()
                self.state.pop(complete.transfer_id)

            filesize, second = complete.filesize_bytes, int(complete.unix_ts_end)
            fb_secs = complete.elapsed_seconds['first_byte'] - complete.elapsed_seconds['command']
            lb_secs = complete.elapsed_seconds['last_byte'] - complete.elapsed_seconds['command']

            fb_list = self.transfers_summary['time_to_first_byte'].setdefault(filesize, {}).setdefault(second, [])
            fb_list.append(fb_secs)
            lb_list = self.transfers_summary['time_to_last_byte'].setdefault(filesize, {}).setdefault(second, [])
            lb_list.append(lb_secs)

        elif re.search("transfer-error", line) is not None:
            error = TransferErrorEvent(line)

            if not do_simple:
                xfer = self.state.setdefault(error.transfer_id, Transfer(error.transfer_id))
                xfer.add_event(error)
                self.transfers[xfer.id] = xfer.get_data()
                self.state.pop(error.transfer_id)

            err_code, filesize, second = error.error_code, error.filesize_bytes, int(error.unix_ts_end)

            err_list = self.transfers_summary['errors'].setdefault(err_code, {}).setdefault(second, [])
            err_list.append(filesize)

        return True

    def parse(self, source, do_simple=True):
        source.open()
        for line in source:
            # ignore line parsing errors
            try:
                if not self.__parse_line(line, do_simple):
                    break
            except:
                logging.warning("TGenParser: skipping line due to parsing error: {0}".format(line))
                raise
                continue
        source.close()

    def get_data(self):
        return {'transfers':self.transfers, 'transfers_summary': self.transfers_summary}

    def get_name(self):
        return self.name
