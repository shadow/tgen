'''
  tgentools
  Authored by Rob Jansen, 2015
  See LICENSE for licensing information
'''

import sys, os, socket, logging, random, re, shutil, datetime, urllib, gzip
from subprocess import Popen, PIPE, STDOUT
from threading import Lock
from abc import ABCMeta, abstractmethod

try:
    from StringIO import StringIO
except ImportError:
    from io import StringIO

LINE_COLORS = ['k', 'r', 'b', 'g', 'c', 'm', 'y']
LINE_STYLES = ['-', '--', '-.', ':']

# since there are 7 colors and 4 styles (no common factor), there will be 28
# distinct color/style combinations before it repeats
LINE_FORMATS = ','.join([x[0] + x[1] for x in zip(LINE_COLORS*10, LINE_STYLES*10)])

def make_dir_path(path):
    p = os.path.abspath(os.path.expanduser(path))
    if not os.path.exists(p):
        os.makedirs(p)

def find_file_paths(searchpath, patterns):
    paths = []
    if searchpath.endswith("/-"): paths.append("-")
    else:
        for root, dirs, files in os.walk(searchpath):
            for name in files:
                found = False
                fpath = os.path.join(root, name)
                # search only the relative path + filename (relative to the original search path)
                frelpath = os.path.relpath(fpath, searchpath)
                for pattern in patterns:
                    if re.search(pattern, frelpath): found = True
                if found: paths.append(fpath)
    return paths

def find_file_paths_pairs(searchpath, patterns_a, patterns_b):
    paths = []
    for root, dirs, files in os.walk(searchpath):
        for name in files:
            fpath = os.path.join(root, name)
            fbase = os.path.basename(fpath)

            paths_a = []
            found = False
            for pattern in patterns_a:
                if re.search(pattern, fbase):
                    found = True
            if found:
                paths_a.append(fpath)

            paths_b = []
            found = False
            for pattern in patterns_b:
                if re.search(pattern, fbase):
                    found = True
            if found:
                paths_b.append(fpath)

            if len(paths_a) > 0 or len(paths_b) > 0:
                paths.append((paths_a, paths_b))
    return paths

def find_path(binpath, defaultname):
    # find the path to tor
    if binpath is not None:
        binpath = os.path.abspath(os.path.expanduser(binpath))
    else:
        w = which(defaultname)
        if w is not None:
            binpath = os.path.abspath(os.path.expanduser(w))
        else:
            logging.error("You did not specify a path to a '{0}' binary, and one does not exist in your PATH".format(defaultname))
            return None
    # now make sure the path exists
    if os.path.exists(binpath):
        logging.info("Using '{0}' binary at {1}".format(defaultname, binpath))
    else:
        logging.error("Path to '{0}' binary does not exist: {1}".format(defaultname, binpath))
        return None
    # we found it and it exists
    return binpath

def which(program):
    def is_exe(fpath):
        return os.path.isfile(fpath) and os.access(fpath, os.X_OK)
    fpath, fname = os.path.split(program)
    if fpath:
        if is_exe(program):
            return program
    else:
        for path in os.environ["PATH"].split(os.pathsep):
            exe_file = os.path.join(path, program)
            if is_exe(exe_file):
                return exe_file
    return None

def timestamp_to_seconds(stamp):  # unix timestamp
    return float(stamp)

def date_to_string(date_object):
    if date_object is not None:
        return "{:04d}-{:02d}-{:02d}".format(date_object.year, date_object.month, date_object.day)
    else:
        return ""

def do_dates_match(date1, date2):
    year_matches = True if date1.year == date2.year else False
    month_matches = True if date1.month == date2.month else False
    day_matches = True if date1.day == date2.day else False
    if year_matches and month_matches and day_matches:
        return True
    else:
        return False

def get_ip_address():
    ip_address = None

    data = urllib.urlopen('https://check.torproject.org/').read()
    if data is not None and len(data) > 0:
        ip_list = re.findall(r'[\d]{1,3}\.[\d]{1,3}\.[\d]{1,3}\.[\d]{1,3}', data)
        if ip_list is not None and len(ip_list) > 0:
            ip_address = ip_list[0]

    if ip_address is None:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 53))
        ip_address = s.getsockname()[0]
        s.close()

    return ip_address

def get_random_free_port():
    while True:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        port = random.randint(10000, 60000)
        rc = s.connect_ex(('127.0.0.1', port))
        s.close()
        if rc != 0: # error connecting, port is available
            return port

class DataSource(object):
    def __init__(self, filename, compress=False):
        self.filename = filename
        self.compress = compress
        self.source = None
        self.xzproc = None

    def __iter__(self):
        if self.source is None:
            self.open()
        return self.source

    def next(self):
        return self.__next__()

    def __next__(self):  # python 3
        return self.source.next() if self.source is not None else None

    def open(self):
        if self.source is None:
            if self.filename == '-':
                self.source = sys.stdin
            elif self.compress or self.filename.endswith(".xz"):
                self.compress = True
                cmd = "xz --decompress --stdout {0}".format(self.filename)
                xzproc = Popen(cmd.split(), stdout=PIPE)
                self.source = xzproc.stdout
            elif self.filename.endswith(".gz"):
                self.compress = True
                self.source = gzip.open(self.filename, 'rt')
            else:
                self.source = open(self.filename, 'r')

    def get_file_handle(self):
        if self.source is None:
            self.open()
        return self.source

    def close(self):
        if self.source is not None: self.source.close()
        if self.xzproc is not None: self.xzproc.wait()


class Writable(object):
    __metaclass__ = ABCMeta

    @abstractmethod
    def write(self, msg):
        pass

    @abstractmethod
    def close(self):
        pass

class FileWritable(Writable):

    def __init__(self, filename, do_compress=False, do_truncate=False, xz_nthreads=3):
        self.filename = filename
        self.do_compress = do_compress
        self.do_truncate = do_truncate
        self.xz_nthreads = xz_nthreads
        self.file = None
        self.xzproc = None
        self.ddproc = None
        self.lock = Lock()

        if self.filename == '-':
            self.file = sys.stdout
        elif self.do_compress or self.filename.endswith(".xz"):
            self.do_compress = True
            if not self.filename.endswith(".xz"):
                self.filename += ".xz"

    def write(self, msg):
        self.lock.acquire()
        if self.file is None: self.__open_nolock()
        if self.file is not None: self.file.write(msg.encode())
        self.lock.release()

    def open(self):
        self.lock.acquire()
        self.__open_nolock()
        self.lock.release()

    def __open_nolock(self):
        if self.do_compress:
            self.xzproc = Popen(f"xz --threads={self.xz_nthreads} -".split(), stdin=PIPE, stdout=PIPE)
            dd_cmd = "dd of={0}".format(self.filename)
            # # note: its probably not a good idea to append to finalized compressed files
            # if not self.do_truncate: dd_cmd += " oflag=append conv=notrunc"
            self.ddproc = Popen(dd_cmd.split(), stdin=self.xzproc.stdout, stdout=open(os.devnull, 'w'), stderr=STDOUT)
            self.file = self.xzproc.stdin
        else:
            self.file = open(self.filename, 'w' if self.do_truncate else 'a', 0)

    def close(self):
        self.lock.acquire()
        self.__close_nolock()
        self.lock.release()

    def __close_nolock(self):
        if self.file is not None:
            self.file.close()
            self.file = None
        if self.xzproc is not None:
            self.xzproc.wait()
            self.xzproc = None
        if self.ddproc is not None:
            self.ddproc.wait()
            self.ddproc = None

    def rotate_file(self, filename_datetime=datetime.datetime.now()):
        self.lock.acquire()

        # build up the new filename with an embedded timestamp
        base = os.path.basename(self.filename)
        base_noext = os.path.splitext(os.path.splitext(base)[0])[0]
        ts = filename_datetime.strftime("%Y-%m-%d_%H:%M:%S")
        new_base = base.replace(base_noext, "{0}_{1}".format(base_noext, ts))
        new_filename = self.filename.replace(base, "log_archive/{0}".format(new_base))

        make_dir_path(os.path.dirname(new_filename))

        # close and move the old file, then open a new one at the original location
        self.__close_nolock()
        # shutil.copy2(self.filename, new_filename)
        # self.file.truncate(0)
        shutil.move(self.filename, new_filename)
        self.__open_nolock()

        self.lock.release()
        # return new file name so it can be processed if desired
        return new_filename

class MemoryWritable(Writable):

    def __init__(self):
        self.str_buffer = StringIO()

    def write(self, msg):
        self.str_buffer.write()

    def readline(self):
        return self.str_buffer.readline()

    def close(self):
        self.str_buffer.close()
