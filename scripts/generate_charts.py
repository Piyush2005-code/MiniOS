import matplotlib.pyplot as plt
import numpy as np
import os

os.makedirs('docs/images', exist_ok=True)

# Data from benchmark
algorithms = ['FCFS', 'SJF', 'Round-Robin', 'HRRN', 'Priority', 'MLQ', 'Lottery']
total_time = [620, 281, 318, 318, 293, 269, 303]
avg_turnaround = [581, 145, 283, 192, 244, 232, 286]
avg_response = [487, 100, 99, 141, 200, 158, 152]
context_switches = [5, 5, 20, 7, 5, 17, 14]

def autolabel(rects, ax):
    """Attach a text label above each bar in *rects*, displaying its height."""
    for rect in rects:
        height = rect.get_height()
        ax.annotate('{}'.format(height),
                    xy=(rect.get_x() + rect.get_width() / 2, height),
                    xytext=(0, 3),  # 3 points vertical offset
                    textcoords="offset points",
                    ha='center', va='bottom')

# 1. Total Execution Time
fig, ax = plt.subplots(figsize=(10, 6))
bars = ax.bar(algorithms, total_time, color='skyblue')
ax.set_ylabel('Time (μs)')
ax.set_title('Total Execution Time by Algorithm')
autolabel(bars, ax)
plt.savefig('docs/images/chart_total_time.png')
plt.close()

# 2. Average Turnaround Time
fig, ax = plt.subplots(figsize=(10, 6))
bars = ax.bar(algorithms, avg_turnaround, color='lightgreen')
ax.set_ylabel('Time (μs)')
ax.set_title('Average Turnaround Time by Algorithm')
autolabel(bars, ax)
plt.savefig('docs/images/chart_turnaround.png')
plt.close()

# 3. Average Response Time
fig, ax = plt.subplots(figsize=(10, 6))
bars = ax.bar(algorithms, avg_response, color='salmon')
ax.set_ylabel('Time (μs)')
ax.set_title('Average Response Time by Algorithm')
autolabel(bars, ax)
plt.savefig('docs/images/chart_response.png')
plt.close()

# 4. Context Switches
fig, ax = plt.subplots(figsize=(10, 6))
bars = ax.bar(algorithms, context_switches, color='orchid')
ax.set_ylabel('Number of Switches')
ax.set_title('Total Context Switches by Algorithm')
autolabel(bars, ax)
plt.savefig('docs/images/chart_switches.png')
plt.close()

# 5. Combined Metrics (Turnaround vs Response vs Total)
x = np.arange(len(algorithms))
width = 0.25

fig, ax = plt.subplots(figsize=(12, 7))
rects1 = ax.bar(x - width, total_time, width, label='Total Time (μs)', color='skyblue')
rects2 = ax.bar(x, avg_turnaround, width, label='Avg Turnaround (μs)', color='lightgreen')
rects3 = ax.bar(x + width, avg_response, width, label='Avg Response (μs)', color='salmon')

ax.set_ylabel('Time (μs)')
ax.set_title('Comparative Timing Metrics across Scheduling Algorithms')
ax.set_xticks(x)
ax.set_xticklabels(algorithms)
ax.legend()

plt.savefig('docs/images/chart_combined_metrics.png')
plt.close()

print("Charts generated successfully in docs/images/")
