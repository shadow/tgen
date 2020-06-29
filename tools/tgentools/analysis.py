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
from tgentools._version import __version__
import tgentools.util as util

class Analysis(object):

    def __init__(self, nickname=None, ip_address=None):
        self.nickname = nickname
        self.measurement_ip = ip_address
        self.hostname = gethostname().split('.')[0]
        self.json_db = {'type':'tgen', 'version':__version__, 'data':{}}
        self.tgen_filepaths = []
        self.date_filter = None
        self.did_analysis = False

    def add_tgen_file(self, filepath):
        self.tgen_filepaths.append(filepath)

    def get_nodes(self):
        return self.json_db['data'].keys()

    def get_tgen_stream_summary(self, node):
        try:
            return self.json_db['data'][node]['tgen']['stream_summary']
        except:
            return None

    def get_tgen_heartbeats(self, node):
        try:
            return self.json_db['data'][node]['tgen']['heartbeats']
        except:
            return None

    def get_tgen_init_ts(self, node):
        try:
            return self.json_db['data'][node]['tgen']['init_ts']
        except:
            return None

    def analyze(self, do_complete=False, date_filter=None):
        if self.did_analysis:
            return

        self.date_filter = date_filter
        tgen_parser = TGenParser(date_filter=self.date_filter)

        for (filepaths, parser, json_db_key) in [(self.tgen_filepaths, tgen_parser, 'tgen')]:
            if len(filepaths) > 0:
                for filepath in filepaths:
                    logging.info("parsing log file at {}".format(filepath))
                    parser.parse(util.DataSource(filepath), do_complete=do_complete)

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

    def save(self, filename=None, output_prefix=os.getcwd(), do_compress=True):
        if filename is None:
            if self.date_filter is None:
                filename = "tgen.analysis.json.xz"
            else:
                filename = "{}.tgen.analysis.json.xz".format(util.date_to_string(self.date_filter))

        filepath = os.path.abspath(os.path.expanduser("{}/{}".format(output_prefix, filename)))
        if not os.path.exists(output_prefix):
            os.makedirs(output_prefix)

        logging.info("saving analysis results to {}".format(filepath))

        outf = util.FileWritable(filepath, do_compress=do_compress)
        json.dump(self.json_db, outf, sort_keys=True, separators=(',', ': '), indent=2)
        outf.close()

        logging.info("done saving analysis results!")

    @classmethod
    def load(cls, filename="tgen.analysis.json.xz", input_prefix=os.getcwd()):
        filepath = os.path.abspath(os.path.expanduser("{}".format(filename)))
        if not os.path.exists(filepath):
            filepath = os.path.abspath(os.path.expanduser("{}/{}".format(input_prefix, filename)))
            if not os.path.exists(filepath):
                logging.warning("file does not exist at '{}'".format(filepath))
                return None

        logging.info("loading analysis results from {}".format(filepath))

        inf = util.DataSource(filepath)
        inf.open()
        db = json.load(inf.get_file_handle())
        inf.close()

        logging.info("finished loading analysis file, checking type and version")

        if 'type' not in db or 'version' not in db:
            logging.warning("'type' or 'version' not present in database")
            return None
        elif db['type'] != 'tgen':
            logging.warning("type '{}' not supported (expected type='tgen')".format(db['type']))
            return None
        elif db['version'] != __version__:
            logging.warning("version '{}' not supported (expected version='{}')".format(db['version'], __version__))
            return None
        else:
            logging.info("type '{}' and version '{}' is supported".format(db['type'], db['version']))
            analysis_instance = cls()
            analysis_instance.json_db = db
            return analysis_instance

class SerialAnalysis(Analysis):

    def analyze(self, paths, do_complete=False, date_filter=None):
        logging.info("processing input from {} paths...".format(len(paths)))

        analyses = []
        for path in paths:
            a = Analysis()
            a.add_tgen_file(path)
            a.analyze(do_complete=do_complete, date_filter=date_filter)
            analyses.append(a)

        logging.info("merging {} analysis results now...".format(len(analyses)))
        while analyses is not None and len(analyses) > 0:
            self.merge(analyses.pop())
        logging.info("done merging results: {} total nicknames present in json db".format(len(self.json_db['data'])))

def subproc_analyze_func(analysis_args):
    signal(SIGINT, SIG_IGN)  # ignore interrupts
    a = analysis_args[0]
    do_complete = analysis_args[1]
    date_filter = analysis_args[2]
    a.analyze(do_complete=do_complete, date_filter=date_filter)
    return a

class ParallelAnalysis(Analysis):

    def analyze(self, paths, do_complete=False, date_filter=None,
        num_subprocs=cpu_count()):
        logging.info("processing input from {} paths...".format(len(paths)))

        analysis_jobs = []
        for path in paths:
            a = Analysis()
            a.add_tgen_file(path)
            analysis_args = [a, do_complete, date_filter]
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

        logging.info("merging {} analysis results now...".format(len(analyses)))
        while analyses is not None and len(analyses) > 0:
            self.merge(analyses.pop())
        logging.info("done merging results: {} total nicknames present in json db".format(len(self.json_db['data'])))

def parse_tagged_csv_string(csv_string):
    d = {}
    parts = csv_string.strip('[]').split(',')
    for key_value_pair in parts:
        pair = key_value_pair.split('=')
        if len(pair) < 2:
            continue
        d[pair[0]] = pair[1]
    return d

class StreamStatusEvent(object):

    def __init__(self, line):
        self.is_success = False
        self.is_error = False
        self.is_complete = False

        parts = line.strip().split()
        self.unix_ts_end = util.timestamp_to_seconds(parts[2])

        self.transport_info = None if len(parts) < 9 else parse_tagged_csv_string(parts[8])
        self.stream_info = None if len(parts) < 11 else parse_tagged_csv_string(parts[10])
        self.byte_info = None if len(parts) < 13 else parse_tagged_csv_string(parts[12])
        self.time_info = None if len(parts) < 15 else parse_tagged_csv_string(parts[14])

        self.stream_id = "{}:{}:{}:{}".format( \
            self.stream_info['id'] if 'id' in self.stream_info else "None", \
            self.transport_info['fd'] if 'fd' in self.transport_info else "None", \
            self.transport_info['local'] if 'local' in self.transport_info else "None", \
            self.transport_info['remote'] if 'remote' in self.transport_info else "None")

class StreamCompleteEvent(StreamStatusEvent):
    def __init__(self, line):
        super(StreamCompleteEvent, self).__init__(line)
        self.is_complete = True

        time_usec_max = 0.0
        if self.time_info != None:
            for key in self.time_info:
                if 'usecs' in key:
                    val = int(self.time_info[key])
                    time_usec_max = max(time_usec_max, val)
        self.unix_ts_start = self.unix_ts_end - (time_usec_max / 1000000.0)  # usecs to secs

class StreamSuccessEvent(StreamCompleteEvent):
    def __init__(self, line):
        super(StreamSuccessEvent, self).__init__(line)
        self.is_success = True

class StreamErrorEvent(StreamCompleteEvent):
    def __init__(self, line):
        super(StreamErrorEvent, self).__init__(line)
        self.is_error = True

class TGenHeartbeatEvent(object):

    def __init__(self, line):
        parts = line.strip().split()
        self.unix_ts = util.timestamp_to_seconds(parts[2])
        self.info = parse_tagged_csv_string(parts[7])

class Stream(object):
    def __init__(self, tid):
        self.id = tid
        self.last_event = None
        self.payload_recv_progress = {decile:None for decile in [0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]}
        self.payload_send_progress = {decile:None for decile in [0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0]}

    def __set_progress_helper(self, status_event, bytes_key, progress_dict):
        progress = status_event.byte_info[bytes_key].strip('%')
        if progress != '?':
            frac = float(progress)
            # set only the highest decile that we meet or exceed
            for decile in sorted(progress_dict.keys(), reverse=True):
                if frac >= decile:
                    if progress_dict[decile] is None:
                        progress_dict[decile] = status_event.unix_ts_end
                    return

    def add_event(self, status_event):
        self.__set_progress_helper(status_event, 'payload-progress-recv', self.payload_recv_progress)
        self.__set_progress_helper(status_event, 'payload-progress-send', self.payload_send_progress)
        self.last_event = status_event

    def get_data(self):
        e = self.last_event
        if e is None or not e.is_complete:
            return None
        d = e.__dict__
        d['elapsed_seconds'] = {}
        d['elapsed_seconds']['payload_progress_recv'] = {decile: self.payload_recv_progress[decile] - e.unix_ts_start for decile in self.payload_recv_progress if self.payload_recv_progress[decile] is not None}
        d['elapsed_seconds']['payload_progress_send'] = {decile: self.payload_send_progress[decile] - e.unix_ts_start for decile in self.payload_send_progress if self.payload_send_progress[decile] is not None}
        return d

class Parser(object):
    __metaclass__ = ABCMeta
    @abstractmethod
    def parse(self, source, do_complete):
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
        self.heartbeats = {}
        self.heartbeat_seconds = set()
        self.streams = {}
        self.stream_summary = {'time_to_first_byte_recv':{}, 'time_to_last_byte_recv':{},
            'time_to_first_byte_send':{}, 'time_to_last_byte_send':{}, 'errors':{}}
        self.name = None
        self.unix_ts_init = 0
        self.date_filter = date_filter
        self.version_mismatch = False

    def __is_date_valid(self, date_to_check):
        if self.date_filter is None:
            # we are not asked to filter, so every date is valid
            return True
        else:
            # we are asked to filter, so the line is only valid if the date matches the filter
            # both the filter and the unix timestamp should be in UTC at this point
            return util.do_dates_match(self.date_filter, date_to_check)

    def __parse_line(self, line, do_complete):
        if self.name is None and re.search("Initializing\sTGen\sv", line) is not None:
            parts = line.strip().split()

            if len(parts) < 9:
                return True
            self.unix_ts_init = int(util.timestamp_to_seconds(parts[2]))

            version_str = parts[8].strip('v')
            if version_str != __version__:
                self.version_mismatch = True
                logging.warning("Version mismatch: the log file we are parsing was generated using \
                tgen v{}, but this version of tgentools is v{}".format(version_str, __version__))
                return True

            if len(parts) < 18:
                return True
            self.name = parts[17]

        if self.date_filter is not None:
            parts = line.strip().split(' ', 3)
            if len(parts) < 4: # the 3rd is the timestamp, the 4th is the rest of the line
                return True
            unix_ts = float(parts[2])
            line_date = datetime.datetime.utcfromtimestamp(unix_ts).date()
            if not self.__is_date_valid(line_date):
                return True

        elif do_complete and re.search("stream-status", line) is not None:
            status = StreamStatusEvent(line)
            stream = self.state.setdefault(status.stream_id, Stream(status.stream_id))
            stream.add_event(status)

        elif re.search("stream-success", line) is not None:
            complete = StreamSuccessEvent(line)

            if do_complete:
                stream = self.state.setdefault(complete.stream_id, Stream(complete.stream_id))
                stream.add_event(complete)
                self.streams[stream.id] = stream.get_data()
                self.state.pop(complete.stream_id)

            second = int(complete.unix_ts_end)

            if complete.byte_info != None:
                recv_size = int(complete.byte_info['payload-bytes-recv'])
                send_size = int(complete.byte_info['payload-bytes-send'])

                if recv_size > 0 and complete.time_info != None:
                    cmd = int(complete.time_info['usecs-to-command'])
                    fb = int(complete.time_info['usecs-to-first-byte-recv'])
                    lb = int(complete.time_info['usecs-to-last-byte-recv'])

                    fb_recv_secs = (fb - cmd) / 1000000.0  # usecs to secs
                    lb_recv_secs = (lb - cmd) / 1000000.0  # usecs to secs

                    if fb_recv_secs >= 0:
                        fb_list = self.stream_summary['time_to_first_byte_recv'].setdefault(recv_size, {}).setdefault(second, [])
                        fb_list.append(fb_recv_secs)
                    if lb_recv_secs >= 0:
                        lb_list = self.stream_summary['time_to_last_byte_recv'].setdefault(recv_size, {}).setdefault(second, [])
                        lb_list.append(lb_recv_secs)

                if send_size > 0 and complete.time_info != None:
                    cmd = int(complete.time_info['usecs-to-command'])
                    fb = int(complete.time_info['usecs-to-first-byte-send'])
                    lb = int(complete.time_info['usecs-to-last-byte-send'])

                    fb_send_secs = (fb - cmd) / 1000000.0  # usecs to secs
                    lb_send_secs = (lb - cmd) / 1000000.0  # usecs to secs

                    if fb_send_secs >= 0:
                        fb_list = self.stream_summary['time_to_first_byte_send'].setdefault(send_size, {}).setdefault(second, [])
                        fb_list.append(fb_send_secs)
                    if lb_send_secs >= 0:
                        lb_list = self.stream_summary['time_to_last_byte_send'].setdefault(send_size, {}).setdefault(second, [])
                        lb_list.append(lb_send_secs)

        elif re.search("stream-error", line) is not None:
            error = StreamErrorEvent(line)

            if do_complete:
                stream = self.state.setdefault(error.stream_id, Stream(error.stream_id))
                stream.add_event(error)
                self.streams[stream.id] = stream.get_data()
                self.state.pop(error.stream_id)

            if error.stream_info != None and error.byte_info != None:
                err_code = error.stream_info['error']
                if err_code == "PROXY" and error.transport_info != None:
                    terr = error.transport_info['error']
                    tstate = error.transport_info['state']
                    if 'STALLOUT' in terr or 'TIMEOUT' in terr:
                        err_code = "{}-{}-{}".format(err_code, terr, tstate)
                    else:
                        err_code = "{}-{}".format(err_code, terr)
                recv_size = int(error.byte_info['payload-bytes-recv'])
                send_size = int(error.byte_info['payload-bytes-send'])
                second = int(error.unix_ts_end)

                err_list = self.stream_summary['errors'].setdefault(err_code, {}).setdefault(second, [])
                err_list.append((send_size,recv_size))

        elif re.search("driver-heartbeat", line) is not None:
            heartbeat = TGenHeartbeatEvent(line)

            second = int(heartbeat.unix_ts)
            self.heartbeat_seconds.add(second)

            for k in heartbeat.info:
                v = int(heartbeat.info[k])
                self.heartbeats.setdefault(k, {}).setdefault(second, []).append(v)

        return True

    def parse(self, source, do_complete=False):
        source.open()
        for line in source:
            if self.version_mismatch:
                break
            # ignore line parsing errors
            try:
                if not self.__parse_line(line, do_complete):
                    break
            except:
                logging.warning("TGenParser: skipping line due to parsing error: {}".format(line))
                raise
                continue
        source.close()

    def get_data(self):
        # fill in heartbeat info that was not printed in the log because it was 0
        for k in self.heartbeats:
            for second in self.heartbeat_seconds:
                if second not in self.heartbeats[k]:
                    self.heartbeats[k].setdefault(second, [0])

        return {
            'init_ts': self.unix_ts_init,
            'heartbeats': self.heartbeats,
            'streams':self.streams,
            'stream_summary': self.stream_summary
        }

    def get_name(self):
        return self.name
