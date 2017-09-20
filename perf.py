#!/usr/bin/env python

# Copyright 2017 Stanford University
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

###
### Tool to run a performance measurement and upload results
###

from __future__ import print_function

import datetime, json, os, re, sys, subprocess

import github3 # Requires: pip install github3.py

def cmd(command, env=None, cwd=None):
    print(' '.join(command))
    return subprocess.check_output(command, env=env, cwd=cwd)

def get_repository(owner, repository, token):
    session = github3.login(token=token)
    return session.repository(owner=owner, repository=repository)

def create_result_file(repo, path, result):
    content = json.dumps(result)
    repo.create_file(path, 'Add measurement %s.' % path, content)

class ArgvMeasurement(object):
    __slots__ = ['start', 'index', 'filter']
    def __init__(self, start=None, index=None, filter=None):
        if (start is None) == (index is None):
            raise Exception('ArgvMeasurement requires start or index, but not both')
        self.start = int(start) if start is not None else None
        self.index = int(index) if index is not None else None
        if filter is None:
            self.filter = lambda x: x
        elif filter == "basename":
            self.filter = os.path.basename
        else:
            raise Exception('Unrecognized filter "%s"' % filter)
    def measure(self, argv, output):
        if self.start is not None:
            return [self.filter(x) for x in argv[self.start:]]
        elif self.index is not None:
            return self.filter(argv[self.index])
        else:
            assert False

class RegexMeasurement(object):
    __slots__ = ['pattern']
    def __init__(self, pattern=None, multiline=None):
        self.pattern = re.compile(pattern, re.MULTILINE if multiline else None)
    def measure(self, argv, output):
        match = re.search(self.pattern, output)
        if match is None:
            raise Exception('Regex match failed')
        result = match.group(1).strip()
        if len(result) == 0:
            raise Exception('Regex produced empty match')
        return result

class CommandMeasurement(object):
    __slots__ = ['args']
    def __init__(self, args=None):
        self.args = args
    def measure(self, argv, output):
        proc = subprocess.Popen(
            self.args, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        result, _ = proc.communicate(output)
        if proc.returncode != 0:
            raise Exception('Command failed')
        result = result.strip()
        if len(result) == 0:
            raise Exception('Command produced no output')
        return result

measurement_types = {
    'argv': ArgvMeasurement,
    'regex': RegexMeasurement,
    'command': CommandMeasurement,
}

def strip_type(type=None, **kwargs):
    return kwargs

def get_measurement(value, argv, output):
    if 'type' not in value:
        raise Exception('Malformed measurement: Needs field "type"')
    if value['type'] not in measurement_types:
        raise Exception(
            'Malformed measurement: Unrecognized type "%s"' % value['type'])
    measurement = measurement_types[value['type']]
    return measurement(**strip_type(**value)).measure(argv, output)

def get_variable(name, description, optional=False):
    if name not in os.environ:
        if optional:
            return ''
        else:
            raise Exception(
                'Please set environment variable %s to %s' % (name, description))
    return os.environ[name]

def remap_path(source_path, perf_execution_dir):
    run_target = source_path.split('/')
    run_legion_position = run_target.index('legion')
    perf_target = perf_execution_dir.split('/')
    perf_legion_position = perf_target.index('legion')
    remapped_path = '/'.join((perf_target[:perf_legion_position] + run_target[run_legion_position:]))
    return remapped_path

def driver():
    # Parse inputs.
    owner = get_variable('PERF_OWNER', 'Github respository owner')
    repository = get_variable('PERF_REPOSITORY', 'Github respository name')
    access_token = get_variable('PERF_ACCESS_TOKEN', 'Github access token')
    metadata = json.loads(
        get_variable('PERF_METADATA', 'JSON-encoded metadata'))
    measurements = json.loads(
        get_variable('PERF_MEASUREMENTS', 'JSON-encoded measurements'))
    perf_launcher = get_variable('PERF_LAUNCHER', 'perf launcher command').split(' ')
    launcher = get_variable('LAUNCHER', 'process launcher eg aprun', True).split(' ')
    perf_execution_dir = get_variable('PERF_EXECUTION_DIR', 'execution directory', True)

    # Validate inputs.
    if 'benchmark' not in measurements:
        raise Exception('Malformed measurements: Measurement "benchmark" is required')
    if 'argv' not in measurements:
        raise Exception('Malformed measurements: Measurement "argv" is required')

    # Do we need to execute in a special directory?
    if perf_execution_dir != "":
        cmd(['mkdir', '-p', perf_execution_dir])
        lg_rt_dir = os.getenv('LG_RT_DIR')
        assert lg_rt_dir is not None

        path_list = lg_rt_dir.split('/')
        legion_position = path_list.index('legion')
        source_dir = '/'.join(path_list[:legion_position + 1]) 
        destination_list = perf_execution_dir.split('/')
        legion_position = destination_list.index('legion')
        destination_dir = '/'.join(destination_list[:legion_position])

        rsync = cmd(['rsync', '-rLv', '--checksum', source_dir, destination_dir])
        print(rsync)
        pwd = cmd(['pwd']).strip()
        os.chdir(perf_execution_dir)
        run_invocation = [remap_path(sys.argv[1], perf_execution_dir)] + sys.argv[2:]

    # no special directory
    else:
        run_invocation = sys.argv[1:]

    # Run the command
    command_launcher = ''
    if perf_launcher != ['']:
        command_launcher = perf_launcher
    elif launcher != ['']:
        command_launcher = launcher
    command = command_launcher + run_invocation
    output = cmd(command)

    if perf_execution_dir != "":
        os.chdir(pwd)

    # Capture measurements.
    measurement_data = {}
    for key, value in measurements.items():
        measurement_data[key] = get_measurement(value, run_invocation, output)

    # Build result.
    # Move benchmark and argv into metadata from measurements.
    metadata['benchmark'] = measurement_data['benchmark']
    del measurement_data['benchmark']
    metadata['argv'] = measurement_data['argv']
    del measurement_data['argv']
    # Capture measurement time.
    metadata['date'] = datetime.datetime.now().isoformat()
    result = {
        'metadata': metadata,
        'measurements': measurement_data,
    }

    print()
    print('"measurements":', json.dumps(measurement_data, indent=4, sort_keys=True))

    # Insert result into target repository.
    repo = get_repository(owner, repository, access_token)
    path = os.path.join('measurements', metadata['benchmark'], '%s.json' % metadata['date'])
    create_result_file(repo, path, result)

if __name__ == '__main__':
    driver()

