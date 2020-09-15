# Release planning - use case  "Acceleration of Java based microservices"

This is a working document for "use case 1" ( Acceleration of Java based microservices) from [Kontain Platform Requirments](https://docs.google.com/document/d/1LPeGZEuRdgeGx-fvsZ3Gs8ltYp6xOB7MCk10zFwtpsE/edit#)

## Status: draft

## Goal

Installable package that allows customers to run their services under Kontain and take snapshots, and then start them from snapshot without Java boot warm-up overhead.

## Constraints and assumptions

* Customer packages it’s spring-boot application inside an alpine container.
* Customer application has a HTTP endpoint for health check or readiness probing.
* Customer’s host should be able to launch the container.

## Requirements

### Input

* Customer container image, packaged using alpine as a base.
* A manifest specifying a few information:
  * How to launch and tear down the container? We need information on how to run the container in order to take a snapshot. It may be a docker run or a docker compose. The container may require extra dependencies to run.
  * URL to health check or readiness probe. The format can follow k8s health check configuration in YAML.
  * Name of resulting image. We should not destroy the input image.

### Output

* A docker container with km snapshot.
  * Ideally we should not need to change any other configs of the container, with the resulting name specified in the manifest.

### Note and Questions

* This step should be manually invoked after the build system.
* There are two steps in the conversion.
  * The first is to convert from Container to Kontainer.
  * The second step is to create the snapshot of the Kontainer.
* These steps work for workloads other than Java as well in the future. Only the conversion step is Java specific.
* Container is a better input than JAR file. There are too many things to be captured in addition to JAR, but a docker container captures almost all.

## Work items and costs

Costs estimation is TBD

P0:

* Update Java source we are building from. We build from 11.0.6+10, Fedora 31 has 11.0.7.10-0, github at 11.0.8-ga.
* Faktory implementation to convert container to kontainer. One version of Java is required.
* Faktory implementation to produce snapshot from converted kontainer.
  * Require a design of the input and workflow.
* Document the API used to create the snapshot file.
* Benchmarks. Replicate and enhance Springboot vs X. eg. https://medium.com/better-programming/which-java-microservice-framework-should-you-choose-in-2020-4e306a478e58
* pre-take and post-recover hooks (in Java).
  * P0 for design / API , implementation delayed until customer feedback

P1-P2:

* P1: Minimize Java installed components in Kontainer. Currently we include everything JDK.
  * https://stackoverflow.com/questions/51403071/create-jre-from-openjdk-windows talks about using the ‘jlink’ command to create a minimal environment. The ‘jdep’ command will give a list of the components needed.
* P1: Java Test Suite.
  * Need to validate our Java binaries, unless we move to unmodified binaries.
* P1: KM mmap alignment.
  * We run with CompressedOOPS disabled so we don’t fall into pathological behavior b/c we don’t support mmap hints. Fix this?
* err msg if assumptions are violated (e.g. JDBC connection is pulled)
  * Decision: P1: support/document payload API (including Java support a snapshot on client calling  a dedicated API to the endpoint
* P2: Integration with customer build system (maven and/or gradle).
  * 7/28 * Build systems (maven/gradle) - postponed …. No actions in the first release

## Open Items

Open items may be resolved after an explicit investigation and discussion, or after some time passing and us getting enough data

* How do we deal with CONFIG and SECRETS (maybe helped by hooks)
  * Secrets read on startup and might be frozen in snapshot
  * Secrets are read from mounts and may use inotify
* Production vs. Preparation: configuration during snapshot capture has to be production one
* Do we support/use inotify to the payload  ?
  * Also , can we issue inotify for open files on snapshot restore ?
* How do we deal with env vars changing  ?
  * e.g. “replica_number” env which is different per invocation and used internally for forming DB key (e.g. Kafka/confluent)
* Supporting GLIBC and using openjdk pre-built vs. building and test multiple versions ourselves  in the initial product
  * Decision: use the latest fixed kontain-build for now (11.0.8+10) add investigation/decision/execution to after the initial product is out
* Choice: keep a dedicated socket as today.