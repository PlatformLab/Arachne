# This script provides convenience command bta (backtrace-arachne) to backtrace
# an Arachne thread, given a ThreadContext* as an argument.

import gdb
class BackTraceArachneCommand (gdb.Command):
  "Backtrace command for user threads in Arachne threading library."

  def __init__ (self):
    super (BackTraceArachneCommand, self).__init__ ("backtrace-arachne",
                         gdb.COMMAND_STACK,
                         gdb.COMPLETE_SYMBOL, True)
    gdb.execute("alias -a bta = backtrace-arachne", True)
  def invoke(self, arg, from_tty):
    arg = arg.strip()
    if arg == "":
        print "Please pass an Arachne::ThreadContext*"
        return
    # Verify that the type is correct
    typestring=gdb.execute("whatis {0}".format(arg), from_tty, True)
    if typestring.strip() != "type = Arachne::ThreadContext *":
        print "Please pass an Arachne::ThreadContext*"
        return

    # Actually perform backtrace
    originalSP = gdb.parse_and_eval("$sp")
    originalPC = str(gdb.parse_and_eval("$pc")).split()[0]
    print "Original SP:", originalSP
    print "Original PC:", originalPC
    gdb.execute("set $sp={0}->sp + Arachne::SpaceForSavedRegisters".format(arg), from_tty)
    gdb.execute("set $pc = *(void **)$rsp", from_tty)
    gdb.execute("backtrace", from_tty)
    gdb.execute("set $sp={0}".format(originalSP), from_tty)
    gdb.execute("set $pc={0}".format(originalPC), from_tty)

BackTraceArachneCommand()
