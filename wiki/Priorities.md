# Priorities and Starvation

## Open Questions

 - Are priorities necessary for good performance within an application in
   addition to between applications?
 - What is a reasonable policy concerning starvation?
 - What is the performance cost of having arbitrary priority levels?

## Ideas for preventing starvation

1. Require that the number of CPU-bound high priority threads is lower than the
   number of cores.
2. Ensure there is at least one core to run only low priority threads, even if
   that means putting more than one high-priority thread on the same core.
