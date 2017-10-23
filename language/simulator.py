import time

lines = []
with open('circuit_log', 'r') as f:
    for line in f:
        lines.append(line)


with open('circuit_log_live', 'w') as f:
    f.write('')
i = 0
while (True):
    with open('circuit_log_live', 'a') as f:
        f.write(lines[i])
    time.sleep(0.01)
    i += 1
