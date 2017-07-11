# This script provides a convenience command ta (thread-arachne) to switch to
# an Arachne thread, given a ThreadContext* as an argument.

import gdb
class ArachneDiffStackCommand (gdb.Command):
  "Thread switch command for user threads in Arachne threading library."

  def __init__ (self):
    super (ArachneDiffStackCommand, self).__init__ ("arachne-diff-stack",
                         gdb.COMMAND_STACK,
                         gdb.COMPLETE_NONE, True)

  def invoke(self, arg, from_tty):
    # Check if the provided threadcontext is NULL, and do nothing if it is.
    for i in xrange(56):
        print gdb.parse_and_eval("Arachne::localThreadContexts[{0}]->sp - Arachne::localThreadContexts[{0}]->stack".format(i))

ArachneDiffStackCommand()

