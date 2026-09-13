import sys
import time
from tqdm import tqdm
from tqdm import trange

# badge
with open("./badge.txt", "r") as file:
    print(file.read())

# global
sim_pcs = []
real_pcs = []

# config
PATH = False  # generate path or not

# start_pc is to align the specific real device start pc
# For example, cc2538_rf_irq_handler may trigger many times in real, but read packet is just one of them, in contrast, simulator only trigger once
start_pc = 0
end_pc = 0
# skip cannot record pcs, for example pc before pendsv
# !!! skip pc before pendsv and set PENDSVSET
skip_pcs = set()


# SIM
def sim_read(f_):
    global sim_pcs
    global start_pc
    global end_pc
    with open(f_, "r") as file:
        lines = file.readlines()

    start = True
    prev_ = 0
    for l_ in lines:
        if l_[:6] != "[State":
            continue
        s_ = l_.split()
        if len(s_) < 4:
            continue
        pc_ = int(s_[4][2:], 16)
        if pc_ in skip_pcs:
            continue
        if not start and pc_ == start_pc:
            start = True
        if start and prev_ != pc_:
            sim_pcs.append(pc_)
            # avoid simulator caused trace instruction twice,
            # for example mmio read will cause two same pc record
            prev_ = pc_
        if pc_ == end_pc:
            break
    return


# sim files
sim_read("./irq_sim.log")
sim_read("./main_sim.log")


# Real
def real_read(f_):
    global real_pcs
    global start_pc
    global end_pc
    with open(f_, "r") as file:
        lines = file.readlines()

    start = True
    for l_ in lines:
        if l_[:2] != "0x":
            continue
        pc_ = int(l_[2:], 16)
        if pc_ in skip_pcs:
            continue
        if not start and pc_ == start_pc:
            start = True
        if start:
            real_pcs.append(pc_)
        if pc_ == end_pc:
            break
    return


# real files
real_read("./irq_real.log")
real_read("./main_real.log")


def levenshtein_distance(s1, s2):
    if len(s1) < len(s2):
        return levenshtein_distance(s2, s1)

    if len(s2) == 0:
        return len(s1)

    previous_row = range(len(s2) + 1)
    for i, c1 in tqdm(enumerate(s1), total=len(s1)):
        print("TESTSTETET")
        current_row = [i + 1]
        for j, c2 in enumerate(s2):
            insertions = previous_row[j + 1] + 1
            deletions = current_row[j] + 1
            substitutions = previous_row[j] + (c1 != c2)
            val_ = min(insertions, deletions, substitutions)
            current_row.append(val_)
        previous_row = current_row

    return previous_row[-1]


# levenshtein with path generation

INSERT = 1
DELETE = 2
REPLACE = 3


def levenshtein_distance_and_path(s1, s2):
    m, n = len(s1), len(s2)
    dp = [[0] * (n + 1) for _ in range(m + 1)]

    for i in range(m + 1):
        dp[i][0] = i
    for j in range(n + 1):
        dp[0][j] = j

    for i in trange(1, m + 1):
        for j in range(1, n + 1):
            if s1[i - 1] == s2[j - 1]:
                dp[i][j] = dp[i - 1][j - 1]
            else:
                dp[i][j] = min(dp[i - 1][j], dp[i][j - 1], dp[i - 1][j - 1]) + 1

    stime = time.time()
    path = []
    i, j = m, n
    while i > 0 and j > 0:
        if s1[i - 1] == s2[j - 1]:
            i -= 1
            j -= 1
        elif dp[i][j] == dp[i - 1][j - 1] + 1:
            path.append([i, REPLACE, s1[i - 1], s2[j - 1]])
            i -= 1
            j -= 1
        elif dp[i][j] == dp[i - 1][j] + 1:
            path.append([i, DELETE, s1[i - 1]])
            i -= 1
        else:
            path.append([i, INSERT, s2[j - 1]])
            j -= 1

    while i > 0:
        path.append([i, DELETE, s1[i - 1]])
        i -= 1
    while j > 0:
        path.append([i, INSERT, s2[j - 1]])
        j -= 1

    path.reverse()
    print(f" path gen time {time.time() - stime}")
    return dp[m][n], path


if PATH:
    distance, path = levenshtein_distance_and_path(sim_pcs, real_pcs)
    ratio = 1 - (distance / max(len(sim_pcs), len(real_pcs)))
    print(f"Levenshtein distance {distance} ratio {ratio}")
    print("Transformation path:")
    for step in path:
        if step[1] == REPLACE:
            print(f"At position {step[0]} replace {step[2]:#x} with {step[3]:#x}")
        elif step[1] == INSERT:
            print(f"At position {step[0]} insert {step[2]:#x}")
        elif step[1] == DELETE:
            print(f"At position {step[0]} delete {step[2]:#x}")
else:
    distance_ = levenshtein_distance(real_pcs, sim_pcs)
    length_ = len(real_pcs)
    ratio_ = 1 - (distance_ / length_)
    print(f"distance {distance_} ratio {ratio_*100:.2f} len {length_}")
