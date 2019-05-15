# cs170-project3
Fuheng Zhao:
in pthread join, i initialy used lock and unlock to avoid preemption and force the thread it was joining to be finished before anything else.

However, this approach slower down the performance as the there will be no more parrellel in joining.
