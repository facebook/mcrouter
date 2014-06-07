# Copyright (c) 2014, Facebook, Inc.
#  All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function
from __future__ import unicode_literals

import atexit
import errno
import os
import random
import re
import select
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import pdb

from mcrouter.test.config import McrouterGlobals

class BaseDirectory(object):
    def __init__(self, prefix="mctest"):
        self.path = tempfile.mkdtemp(prefix=prefix + '.')

    def __del__(self):
        shutil.rmtree(self.path)

def MCPopen(cmd, stdout=None, stderr=None, env=None):
    return subprocess.Popen(cmd, stdout=stdout, stderr=stderr, env=env)

class MCProcess(object):
    """
    It would be best to use mc.client and support all requests. But we can't do
    that until mc.client supports ASCII (because mcproxy doesn't support
    binary). For now, be hacky and just talk ASCII by hand.
    """

    proc = None

    def __init__(self, cmd, port, base_dir=None, junk_fill=False):
        port = int(port)

        if base_dir is None:
            base_dir = BaseDirectory('MCProcess')
        self.base_dir = base_dir
        self.stdout = os.path.join(base_dir.path, 'stdout')
        self.stderr = os.path.join(base_dir.path, 'stderr')
        stdout = open(self.stdout, 'w')
        stderr = open(self.stderr, 'w')

        if cmd:
            for command in cmd:
                if command == 'python':
                    continue
                if command.startswith('-'):
                    continue
                command = os.path.basename(command)
                break

            try:
                if junk_fill:
                    env = dict(MALLOC_CONF='junk:true')
                else:
                    env = None
                self.proc = MCPopen(cmd, stdout, stderr, env)

            except OSError:
                sys.exit("Fatal: Could not run " + repr(" ".join(cmd)))
        else:
            self.proc = None

        self.addr = ('localhost', port)
        self.port = port
        self.sets = 0
        self.gets = 0
        self.deletes = 0
        self.others = 0

    def pause(self):
        self.proc.send_signal(signal.SIGSTOP)

    def unpause(self):
        self.proc.send_signal(signal.SIGCONT)

    def getport(self):
        return self.port

    def connect(self):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect(self.addr)
        self.fd = self.socket.makefile()

    def ensure_connected(self):
        while True:
            try:
                self.connect()
                return
            except Exception, e:
                if e.errno == errno.ECONNREFUSED:
                    pass
                else:
                    raise

    def disconnect(self):
        try:
            self.socket.close()
        except IOError:
            pass
        try:
            self.fd.close()
        except IOError:
            pass
        self.fd = self.socket = None

    def terminate(self):
        if not self.proc:
            return None

        self.disconnect()

        self.dump()

        proc = self.proc
        if self.proc:
            self.proc.terminate()
            self.proc.wait()
            self.proc = None

        return proc

    def is_alive(self):
        self.proc.poll()
        return self.proc.returncode is None

    def dump(self):
        """ dump stderr, stdout, and the log file to stdout with nice headers.
        This allows us to get all this information in a test failure (hidden by
        default) so we can debug better. """

        # Grumble... this would be so much easier if I could just pass
        # sys.stdout/stderr to Popen.
        with open(self.stdout, 'r') as stdout_f:
            stdout = stdout_f.read()

        with open(self.stderr, 'r') as stderr_f:
            stderr = stderr_f.read()

        if hasattr(self, 'log'):
            print(self.base_dir)
            try:
                with open(self.log, 'r') as log_f:
                    log = log_f.read()
            except:
                log = ""
        else:
            log = ""

        if log:
            print("%s stdout:\n%s" % (self, log))
        if stdout:
            print("%s stdout:\n%s" % (self, stdout))
        if stderr:
            print("%s stdout:\n%s" % (self, stderr))

    def __del__(self):
        total = self.gets + self.sets + self.deletes + self.others
        if self.proc:
            self.proc.terminate()

    def get(self, keys, return_all_info=False):
        multi = True
        if not isinstance(keys, list):
            multi = False
            keys = [keys]
        self.gets += len(keys)
        self.socket.sendall("get %s\r\n" % " ".join(keys))
        res = dict([(key, None) for key in keys])

        while True:
            l = self.fd.readline().strip()
            if l == 'END':
                if multi:
                    return res
                else:
                    assert len(res) == 1
                    return res.values()[0]
            elif l.startswith("VALUE"):
                v, k, f, n = l.split()
                assert k in keys
                payload = self.fd.read(int(n))
                self.fd.read(2)
                if (return_all_info):
                    res[k] = dict({"key": k,
                                  "flags": int(f),
                                  "size": int(n),
                                  "value": payload})
                else:
                    res[k] = payload
            elif l.startswith("SERVER_ERROR"):
                return l
            else:
                self.connect()
                raise Exception('Unexpected response "%s" (%s)' % (l, keys))

    def metaget(self, keys):
        ## FIXME: Not supporting multi-metaget yet
        #multi = True
        #if not instance(keys, list):
        #    multi = False
        #    keys = [keys]
        #self.gets += len(keys)
        res = {}
        self.gets += 1
        self.socket.sendall("metaget %s\r\n" % keys)

        while True:
            l = self.fd.readline().strip()
            if l.startswith("END"):
                return res
            elif l.startswith("META"):
                meta_list = l.split()
                for i in range(1, len(meta_list) // 2):
                    res[meta_list[2 * i].strip(':')] = \
                        meta_list[2 * i + 1].strip(';')

    def leaseGet(self, keys):
        multi = True
        if not isinstance(keys, list):
            multi = False
            keys = [keys]
        self.gets += len(keys)
        self.socket.sendall("lease-get %s\r\n" % " ".join(keys))
        res = dict([(key, None) for key in keys])

        while True:
            l = self.fd.readline().strip()
            if l == 'END':
                if multi:
                    assert(len(res) == len(keys))
                    return res
                else:
                    assert len(res) == 1
                    return res.values()[0]
            elif l.startswith("VALUE"):
                v, k, f, n = l.split()
                assert k in keys
                res[k] = {"value": self.fd.read(int(n)),
                          "token": None}
                self.fd.read(2)
            elif l.startswith("LVALUE"):
                v, k, t, f, n = l.split()
                assert k in keys
                res[k] = {"value": self.fd.read(int(n)),
                          "token": int(t)}

    def _set(self, command, key, value, replicate=False):
        self.sets += 1
        value = str(value)
        flags = 1024 if replicate else 0
        self.socket.sendall("%s %s %d 0 %d\r\n%s\r\n" %
                            (command, key, flags, len(value), value))

        answer = self.fd.readline().strip()
        if re.search('ERROR', answer):
            print(answer)
            self.connect()
            return None
        return re.match("STORED", answer)

    def leaseSet(self, key, value_token, is_stalestored=False):
        self.sets += 1
        value = str(value_token["value"])
        token = int(value_token["token"])
        flags = 0
        cmd = "lease-set %s %d %d 0 %d\r\n%s\r\n" % \
                (key, token, flags, len(value), value)
        self.socket.sendall(cmd)

        answer = self.fd.readline().strip()
        if re.search('ERROR', answer):
            print(answer)
            self.connect()
            return None
        if is_stalestored:
            return re.match("STALE_STORED", answer)
        return re.match("STORED", answer)

    def set(self, key, value, replicate=False):
        return self._set("set", key, value, replicate)

    def add(self, key, value, replicate=False):
        return self._set("add", key, value, replicate)

    def replace(self, key, value, replicate=False):
        return self._set("replace", key, value, replicate)

    def delete(self, key):
        self.socket.sendall("delete %s\r\n" % key)
        self.deletes += 1

        answer = self.fd.readline()

        assert re.match("DELETED|NOT_FOUND", answer), answer
        return re.match("DELETED", answer)

    def incr(self, key, value=1):
        self.socket.sendall("incr %s %d\r\n" % (key, value))
        self.sets += 1

        answer = self.fd.readline()
        if re.match("NOT_FOUND", answer):
            return None
        else:
            return int(answer)

    def decr(self, key, value=1):
        self.socket.sendall("decr %s %d\r\n" % (key, value))
        self.sets += 1

        answer = self.fd.readline()
        if re.match("NOT_FOUND", answer):
            return None
        else:
            return int(answer)

    def stats(self, spec=None):
        q = 'stats\r\n'
        if spec:
            q = 'stats {0}\r\n'.format(spec)
        self.socket.sendall(q)

        s = {}
        l = None
        fds = select.select([self.fd], [], [], 1.0)
        if len(fds[0]) == 0:
            return None
        while l != 'END':
            l = self.fd.readline().strip()
            a = l.split(None, 2)
            if len(a) == 3:
                s[a[1]] = a[2]

        return s

    def issue_command(self, command):
        self.others += 1
        self.socket.sendall(command)
        answer = self.fd.readline()
        return answer

    def version(self):
        self.socket.sendall("version\r\n")
        return self.fd.readline()

    def pause(self):
        if self.proc:
            self.proc.send_signal(signal.SIGSTOP)

    def resume(self):
        if self.proc:
            self.proc.send_signal(signal.SIGCONT)


def sub_port(s, substitute_ports, port_map):
    parts = s.split(':')
    if len(parts) < 2:
        return s

    for i in (-1, -2):
        try:
            port = int(parts[i])
            if port not in port_map:
                if len(port_map) < len(substitute_ports):
                    if isinstance(substitute_ports, list):
                        port_map[port] = substitute_ports[len(port_map)]
                    else:
                        if port not in substitute_ports:
                            raise Exception(
                                "Port %s not in substitute port map" % port)
                        port_map[port] = substitute_ports[port]
                else:
                    raise Exception("Looking up port %d: config file has more"
                                    " ports specified than the number of"
                                    " mock servers started" % port)
            parts[i] = str(port_map[port])
        except (IndexError, ValueError):
            pass
    return ':'.join(parts)


def replace_ports(json, substitute_ports):
    """In string json (which must be a valid JSON string), replace all ports in
    strings of the form "host:port" with ports from the list or map
    substitute_ports.

    If list, each new distinct port from the json will be replaced from the
    next port from the list.

    If map of the form (old_port: new_port), replaces all old_ports with
    new_ports.
    """
    NORMAL = 0
    STRING = 1
    ESCAPE = 2

    state = NORMAL
    out = ""
    s = ""
    port_map = {}
    for c in json:
        if state == NORMAL:
            out += c
            if c == '"':
                s = ""
                state = STRING
        elif state == STRING:
            if c == '\\':
                s += c
                state = ESCAPE
            elif c == '"':
                out += sub_port(s, substitute_ports, port_map)
                out += c
                state = NORMAL
            else:
                s += c
        elif state == ESCAPE:
            s += c
            state = NORMAL

    if len(port_map) < len(substitute_ports):
        raise Exception("Config file has fewer ports specified than the number"
                        " of mock servers started")
    return out

def replace_strings(json, replace_map):
    for (key, value) in replace_map.items():
        json = json.replace(key, str(value))
    return json

class Mcrouter(MCProcess):
    def __init__(self, config, port=None, default_route=None, extra_args=None,
                 base_dir=None, substitute_config_ports=None,
                 substitute_port_map=None, replace_map=None):
        if base_dir is None:
            base_dir = BaseDirectory('mcrouter')
        self.base_dir = base_dir

        self.log = os.path.join(self.base_dir.path, 'mcrouter.log')

        self.async_spool = os.path.join(self.base_dir.path, 'spool')
        os.mkdir(self.async_spool)
        if replace_map:
            with open(config, 'r') as config_file:
                replaced_config = replace_strings(config_file.read(),
                                                  replace_map)
            config = tempfile.mktemp(dir=self.base_dir.path)
            with open(config, 'w') as config_file:
                config_file.write(replaced_config)

        if substitute_config_ports:
            with open(config, 'r') as config_file:
                replaced_config = replace_ports(config_file.read(),
                                                substitute_config_ports)
            config = tempfile.mktemp(dir=self.base_dir.path)
            with open(config, 'w') as config_file:
                config_file.write(replaced_config)

        self.config = config
        args = [McrouterGlobals.InstallDir + '/mcrouter/mcrouter', '-d',
                '-f', config,
                '-L', self.log,
                '-a', self.async_spool]

        listen_sock = None
        if port is None:
            listen_sock = socket.socket()
            listen_sock.listen(100)
            port = listen_sock.getsockname()[1]
            args.extend(['--listen-sock-fd', str(listen_sock.fileno())])
        else:
            args.extend(['-p', str(port)])

        if (default_route):
            args.extend(['-R', default_route])

        if extra_args:
            args.extend(extra_args)

        args = McrouterGlobals.preprocessArgs(args)

        MCProcess.__init__(self, args, port, self.base_dir, junk_fill=True)

        if listen_sock is not None:
            listen_sock.close()

class McrouterClient(MCProcess):
    def __init__(self, port):
        MCProcess.__init__(self, None, str(port))

class Memcached(MCProcess):
    def __init__(self, port=None):
        args = [McrouterGlobals.InstallDir +
                    '/mcrouter/lib/network/mock_mc_server']
        listen_sock = None
        if port is None:
            listen_sock = socket.socket()
            listen_sock.listen(100)
            port = listen_sock.getsockname()[1]
            args.extend(['-t', str(listen_sock.fileno())])
        else:
            args.extend(['-P', str(port)])

        MCProcess.__init__(self, args, port)

        if listen_sock is not None:
            listen_sock.close()
