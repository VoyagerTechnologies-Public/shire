# About SHIRE

## Why SHIRE exists

Satellite software is often developed by several disciplines that depend on interfaces, hardware, and operational tools owned by other teams.
Waiting for every physical component to become available can move integration problems late into a mission schedule.

The Software & Hardware Integration Runtime Environment (SHIRE) provides a shared software lab for that integration work.
It connects flight software, simulated devices, vehicle dynamics, security processing, and ground software before the complete physical system is available.

SHIRE is intended to move interface discovery, software testing, procedure development, and operator familiarization earlier in the mission lifecycle.
It does not replace qualification or testing with representative hardware.

## Design approach

SHIRE follows several practical ideas that are visible in the current repository:

* **Keep component artifacts together.**
  Each reference component owns its cFS application, simulated device, developer CLI, tests, configuration template, and YAMCS artifacts under `comp/`.
* **Use one coordinated simulation clock.**
  Simulith coordinates flight software and the Director while the Director advances component simulators and exchanges state with 42.
* **Generate a repeatable lab from configuration.**
  Mission, spacecraft, and scenario selections produce the compose files, component settings, 42 input, and cFS mission definitions used by a run.
* **Exercise focused interfaces before the complete stack.**
  A component CLI can work directly with one simulator before the component is exercised through cFS and YAMCS.
* **Keep implementation claims traceable.**
  The Atlas links behavior to source, configuration, tests, generated artifacts, or recorded verification evidence.

## Who SHIRE serves

| Discipline | How SHIRE can help |
| --- | --- |
| Software development | Develop cFS applications and device protocols against repeatable component simulators before hardware is generally available. |
| Integration and test | Exercise command, telemetry, CCSDS, CFDP, timing, and device interface boundaries across the stack. |
| Mission operations | Develop and rehearse YAMCS procedures against live simulated telemetry and flight software. |
| Verification and validation | Run unit, component simulator, and integrated scenario tests without consuming scarce hardware time. |
| Training | Give new personnel a resettable environment in which they can observe system behavior and practice procedures. |
| Research | Integrate algorithms or payload concepts into a representative flight, ground, and simulation stack and inspect system effects. |
| Cybersecurity | Exercise isolated command paths, CryptoLib integration, simulator fault injection, and recovery procedures without connecting flight hardware. |

## Current boundaries

The current SHIRE baseline is a development and integration environment.
It does not, by itself, establish flight qualification, hardware compatibility, requirements compliance, performance on a particular host, or suitability for a specific mission.

Component simulators model software interfaces and selected behavior rather than every electrical, radio frequency, thermal, mechanical, or radiation effect.
The ARM and CPU2 files provide development scaffolding, but the default tested path uses the Linux simulation target.
Host resources, networking, and Docker behavior can affect observed timing and transfer performance.

Claims about simulation rate, pointing accuracy, transfer rate, fault response, or hardware portability need a defined configuration and recorded evidence.
See [Space Systems](manual/core-concepts/space-systems.md) for the modeled hardware boundary and [Verification and Validation](drm/verification-and-validation.md) for current evidence entry points.

## Reading the Atlas

Pages describing the current implementation are checked against the public repository.
The Design Reference Mission also includes intended behavior and proposed requirements that have not all been implemented or verified.
Status notes identify that distinction where it matters.

When documentation and the current checkout disagree, the source, configuration, generated artifacts, and tests take precedence.
