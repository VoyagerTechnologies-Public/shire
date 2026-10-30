# Monte Carlo Analysis

## Mission brief

The Sun pointing check passed, and the team is pleased.
Then an engineer asks the question that every single run leaves open, which is how much of that result is the design and how much is the one starting state it happened to have.
Answering it takes many runs that start slightly differently, and a plain account of what their spread can and cannot show.
This is the tenth lesson in the [lesson plan](overview.md#the-lesson-plan), in the Engineering and analysis unit.
The engineering lessons work in a terminal and in files, so this one shows command output in place of screenshots.

## Objective

In this scenario, you will learn how to run a Monte Carlo campaign and read what it reports.
As an engineer, your job is to read a campaign file, run a small campaign that sweeps the initial tumble of the spacecraft, read its report and its dataset, run it again with the same seed and with another one, and state what the spread of results does and does not prove.

## Prerequisites

This lesson needs three earlier lessons, [Commissioning](commissioning.md), [Simulation Time and Repeatability](simulation-time-repeatability.md), and [Attitude Control](attitude-control.md).
You use these skills from them:

* Reading an initial condition and knowing what a scenario does with it
* Knowing that a run depends on its starting state and on when its commands are sent
* Knowing what Sun pointing is, and how a stack checks it against a limit

Before you begin, ensure you have:

* Completed [Getting Started](../manual/handbook/getting-started.md), and run `make` once so the base images exist
* A host with several gigabytes of free memory, because each trial runs its own copy of the whole lab
* About 45 minutes, most of it waiting for campaigns

This lesson starts from a clean checkout, and it does not need the lab running.
Stop any running lab with `make stop` before you start, because the campaign runs its own labs.

## Overview

A Monte Carlo campaign runs one fixed scenario many times.
Each run, called a trial, starts from its own slightly different initial condition, which is drawn at random from distributions that you choose.
Afterward the campaign gathers the results of all the trials into one report.

| Part of a campaign | What it does |
| --- | --- |
| Scenario | The fixed exercise that every trial runs, which is not changed |
| Base initial condition | The starting state that each trial departs from |
| Parameters | The values to vary, each with a distribution |
| Seed | The number that starts the random draws, so the same seed draws the same inputs |
| Trial count | How many runs to make |
| Metrics | Values to read out of each trial and add to the report |

The scenario in this lesson is `adcs-truth-verification`, which commands the ADCS into Sun pointing and checks that the Sun vector converges.
The campaign changes the tumble rate that the spacecraft has at the start, and it measures how many simulated seconds the convergence check takes.

You will follow these phases:

* Read the campaign
* Run the campaign
* Read the report
* Run it again with the same seed
* Run it with another seed
* Decide what the spread shows

The numbers below come from one set of runs.
Your values will differ, and that is part of what the lesson shows.

## Read the campaign

Open `cfg/drm/campaigns/adcs-tumble-sweep.yaml`.

```yaml
name: "adcs-tumble-sweep"
scenario: "adcs-truth-verification"
base_initial_conditions: "nominal-baseline"
trial_count: 6
seed: 20261029
max_parallel: 2

parameters:
  - path: "attitude.angular_velocity_deg_s.0"
    distribution: {type: "uniform", low: -2.0, high: 2.0}
  - path: "attitude.angular_velocity_deg_s.1"
    distribution: {type: "uniform", low: -2.0, high: 2.0}
  - path: "attitude.angular_velocity_deg_s.2"
    distribution: {type: "uniform", low: -2.0, high: 2.0}

metrics:
  - name: "time_to_sun_pointed_s"
    stack: "AdcsTruthComparison"
    step_index: 3
    field: "sim_elapsed_s"
```

Three things in it need care.
Each parameter is a dotted path into the initial condition, and the last number in a path picks an axis of the tumble rate.
The distribution is `uniform` between `-2` and `2`, and a uniform draw is added to the base value by default.
The `nominal-baseline` bin tumbles at `[5.0, -5.0, 10.0]` degrees per second, so the trials tumble at about 3 to 7, minus 3 to minus 7, and 8 to 12 degrees per second.
The metric reads one number out of one step of the `AdcsTruthComparison` stack, which is the simulated time that the Sun vector check spent waiting for the vector to converge.

## Run the campaign

Run it from the repository root:

```bash
make campaign CAMPAIGN=adcs-tumble-sweep
```

The campaign prints its four phases as it goes.

```text
[campaign] adcs-tumble-sweep: 6 trial(s), seed=20261029, max_parallel=2 (~34 cpu-units / ~17GB reserved concurrently)
[campaign] Phase 1/4: sampling parameters...
[campaign] Phase 2/4: building images...
[campaign] 6 unique build key(s) for 6 trial(s)
[campaign] Phase 3/4: running trials (max_parallel=2)...
[campaign] trial 0 (instance=0000, port_offset=0) starting...
[campaign] trial 1 (instance=0001, port_offset=10) starting...
[campaign] trial 0 (instance=0000) finished: pass=True, metrics={'time_to_sun_pointed_s': 31.0}
...
[campaign] Phase 4/4: aggregating...
[campaign] 6/6 trial(s) passed (0 errored)
```

Sampling draws every trial's input before any trial runs.
Building makes one image for each distinct starting state, and every trial here is distinct, so there are six builds.
Running starts two labs at a time, and the numbers in the first line are what they reserve, which is why a small host should lower the count with `MAX_PARALLEL=1`.
This campaign took about four minutes on the host that wrote this lesson.
Do not start another lab while it runs, because each trial uses its own ports and the host needs the memory.

## Read the report

The campaign writes its results under `build/monte-carlo-runs/`, in a directory named for the campaign and the time.
Read the summary of the report.

```bash
python3 -c "import json,sys; print(json.dumps(json.load(open(sys.argv[1]))['summary']['metrics'], indent=1))" \
  build/monte-carlo-runs/adcs-tumble-sweep-*/campaign_report.json
```

```text
{
 "time_to_sun_pointed_s": {
  "max": 44.0,
  "mean": 30.666666666666668,
  "min": 8.0,
  "stdev": 12.484657250668384
 }
}
```

Every trial passed, so the pass rate is `1.0`, and that tells you only that the check was met within its limit every time.
The metric is the more interesting number.
The six trials took between 8 and 44 simulated seconds to point at the Sun, with a mean of about 31 and a standard deviation of about 12.
The campaign also wrote `campaign_dataset.jsonl`, which holds one line for each trial.

```text
{"campaign": "adcs-tumble-sweep", "trial_index": 0, "attitude.angular_velocity_deg_s.0": 5.134621454561259, "attitude.angular_velocity_deg_s.1": -6.097648243941814, "attitude.angular_velocity_deg_s.2": 11.881960521608457, "pass": true, "run_duration_s": 180.0, "time_to_sun_pointed_s": 31.0, ...}
```

Each line has the inputs and the result of one trial, so you can load the file into a spreadsheet or a notebook and look for a relationship between them.
You do not have to run the campaign again to get the file, because `python3 tools/shire-campaign.py --from-report <report> --jsonl-out <path>` writes it again from the report.

## Run it again with the same seed

The seed fixes the inputs, so running the same campaign again should give the same answer.
Run it again.

```bash
make campaign CAMPAIGN=adcs-tumble-sweep
```

The inputs are the same.
The tumble rates of all six trials are identical to the first run, to every digit.
The results are not.

| Trial | First run | Second run |
| --- | --- | --- |
| 0 | 31 | 31 |
| 1 | 34 | 26 |
| 2 | 8 | 31 |
| 3 | 39 | 21 |
| 4 | 44 | 44 |
| 5 | 28 | 28 |

Three trials matched and three did not, and one of them changed from 8 seconds to 31.
The mean and the standard deviation moved too, to about 30 and 8.
The inputs were identical, so the difference did not come from the input.
It came from the run, and a likely cause is that the stack sends its commands at wall clock times, so the same starting tumble meets the Sun pointing command at a slightly different point in the simulation each time.
This lesson did not trace the cause, and that is the point of running the campaign twice.
Part of any spread that a campaign reports is noise from the run itself, and a single campaign cannot tell you how much.

## Run it with another seed

Now change the seed, which draws six different tumbles.
The script takes the option and the `make` target does not.

```bash
python3 tools/shire-campaign.py --campaign adcs-tumble-sweep --seed 20261030
```

The six results were 44, 20, 36, 21, 44, and 34 seconds, with a mean of about 33 and a standard deviation of about 11.
A seed given on the command line replaces the one in the file, so the file no longer describes the run, and you must record the seed beside the result.

## Decide what the spread shows

Put the three campaigns side by side.

| Campaign | Seed | Mean | Standard deviation | Range |
| --- | --- | --- | --- | --- |
| First | 20261029 | 30.7 | 12.5 | 8 to 44 |
| Second | 20261029 | 30.2 | 7.7 | 21 to 44 |
| Third | 20261030 | 33.2 | 10.6 | 20 to 44 |

The means agree to within about three seconds, so they point to a time of roughly 30 seconds.
The standard deviation moved from about 8 to about 12 between two runs of the same inputs, so it is not a stable number from six trials.
No relationship with the tumble appears either, because the trial with the lowest rate on the third axis was the fastest in the first run and in the middle of the others in the second.
A report with six trials supports a statement like this, that the convergence took about 30 seconds in every trial and never failed the check, and run to run it varied by tens of seconds.
It does not support a statement that the design converges within 45 seconds, or that 95 percent of tumbles converge in a given time, because six draws cannot show a tail.

Use the same discipline for any campaign.
Say how many trials there were, say which seed, and say what the numbers do not cover.

## Limitations

This lesson shows how to run and read a campaign, and its result does not qualify the ADCS.
Keep these limits in mind when you read its results:

* Six trials are too few to estimate a tail, a percentile, or a confidence interval, and the standard deviation itself changed between two runs of the same seed.
* The metric includes noise from the run, as the repeat showed, so a difference between two trials is not always a difference in the input.
* The inputs are uniform draws around a base tumble, which the team chose, and the real tumble at release has its own distribution that nobody has measured here.
* Only the tumble rate varies, so the result says nothing about sensor noise, pointing errors, or the Sun geometry.
* The convergence time is measured once each simulated second, so the metric moves in whole seconds.
* A trial that passes means that the check was met and the run completed, and it does not mean the margin was large.
* The campaign is a developer tool, and no CI job runs it.
* The numbers on this page come from one host and one set of runs.

Use the [evidence packet](overview.md#evidence-packet) when you need to retain the result of a run.
See the [Monte Carlo campaigns](../manual/how-to/monte-carlo-campaigns.md) guide for every option and for the report format.

## Review

You can now run a campaign and say what its results do and do not show.

You have:

* Read a campaign file and the way its parameters become the starting state of each trial
* Run a six trial campaign and read its summary and its dataset
* Run the same campaign again and seen that identical inputs gave different results for three trials
* Changed the seed and compared three sets of statistics
* Stated a conclusion that the numbers support, and one that they do not

This lesson adds a habit to the evidence packet.
Record the campaign file, the seed, and the number of trials beside every statistic, so that a reader can tell a stable result from one that six draws produced.
A statistic without its trial count and its seed looks more certain than it is.

Check that you can answer these before you move on:

* Why did the same seed give the same inputs and not the same results?
* What does a pass rate of 100 percent across six trials show, and what does it not show?
* Why can you not state a 95 percent bound from this campaign?

The [lesson plan](overview.md#the-lesson-plan) continues with [New Component Development](new-component-development.md).
There the engineering team adds a new device to the spacecraft.

***
Last reviewed: 20261029
