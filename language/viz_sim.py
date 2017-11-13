from datetime import datetime
from datetime import timedelta
from time import sleep

start_time = datetime.now()
util = 0

# returns the elapsed milliseconds since the start of the program
def millis():
   dt = datetime.now() - start_time
   ms = (dt.days * 24 * 60 * 60 + dt.seconds) * 1000 + dt.microseconds / 1000.0
   return ms

with open('./legion_prof/tsv/0 (CPU)_util.tsv', 'w') as f:
    f.write('time\tcount\n')

with open('./legion_prof/tsv/0 (Utility)_util.tsv', 'w') as f:
    f.write('time\tcount\n')

with open('./legion_prof/tsv/0 (System Memory)_util.tsv', 'w') as f:
    f.write('time\tcount\n')

while True:
    with open('./legion_prof/tsv/0 (CPU)_util.tsv', 'a') as f:
        f.write('{}\t{}\n'.format(millis()*1000, util))

    with open('./legion_prof/tsv/0 (Utility)_util.tsv', 'a') as f:
        f.write('{}\t{}\n'.format(millis()*1000, util))

    with open('./legion_prof/tsv/0 (System Memory)_util.tsv', 'a') as f:
        f.write('{}\t{}\n'.format(millis()*1000, util))

    if util == 0:
        util = 1
    else:
        util = 0
    sleep(0.05)
