# This script provides convenience command bta (backtrace-arachne) to backtrace
# an Arachne thread, given a ThreadContext* as an argument.

from __future__ import print_function

import gdb
class BackTraceArachneCommand (gdb.Command):
  "Backtrace command for user threads in Arachne threading library."

  def __init__ (self):
    super (BackTraceArachneCommand, self).__init__ ("backtrace-arachne",
                         gdb.COMMAND_STACK,
                         gdb.COMPLETE_SYMBOL, True)
    gdb.execute("alias -a bta = backtrace-arachne", True)

  def backtrace(self, threadContext, from_tty):

    # Check if we are backtracing the current context
    loadedContext = gdb.parse_and_eval("Arachne::core.loadedContext")
    if isinstance(threadContext, str):
        threadContext = gdb.parse_and_eval(threadContext)
    if int(loadedContext) == int(threadContext):
        gdb.execute("backtrace", from_tty)
        return

    SP = gdb.parse_and_eval("$sp")
    PC = int(gdb.parse_and_eval("$pc"))
    r12 = int(gdb.parse_and_eval("$r12"))
    r13 = int(gdb.parse_and_eval("$r13"))
    r14 = int(gdb.parse_and_eval("$r14"))
    r15 = int(gdb.parse_and_eval("$r15"))
    rbx = int(gdb.parse_and_eval("$rbx"))
    rbp = int(gdb.parse_and_eval("$rbp"))
    loadedContext = int(gdb.parse_and_eval("Arachne::core.loadedContext"))

    gdb.execute("set Arachne::core.loadedContext = ((Arachne::ThreadContext*){0})".format(threadContext))
    gdb.execute("set $rbp = *(uint64_t*) Arachne::core.loadedContext->sp")
    gdb.execute("set $rbx = *(((uint64_t*) Arachne::core.loadedContext->sp)+1)")
    gdb.execute("set $r15 = *(((uint64_t*) Arachne::core.loadedContext->sp)+2)")
    gdb.execute("set $r14 = *(((uint64_t*) Arachne::core.loadedContext->sp)+3)")
    gdb.execute("set $r13 = *(((uint64_t*) Arachne::core.loadedContext->sp)+4)")
    gdb.execute("set $r12 = *(((uint64_t*) Arachne::core.loadedContext->sp)+5)")
    gdb.execute("set $rsp=Arachne::core.loadedContext->sp + Arachne::SPACE_FOR_SAVED_REGISTERS", from_tty)
    gdb.execute("set $pc = *(void **)$rsp", from_tty)

    gdb.execute("backtrace", from_tty)

    # Restore
    gdb.execute("set  $sp = {0}".format(SP), from_tty)
    gdb.execute("set  $pc = {0}".format(PC), from_tty)
    gdb.execute("set $rbp = {0}".format(rbp), from_tty)
    gdb.execute("set $rbx = {0}".format(rbx), from_tty)
    gdb.execute("set $r15 = {0}".format(r15), from_tty)
    gdb.execute("set $r14 = {0}".format(r14), from_tty)
    gdb.execute("set $r13 = {0}".format(r13), from_tty)
    gdb.execute("set $r12 = {0}".format(r12), from_tty)
    gdb.execute("set Arachne::core.loadedContext = {0}".format(loadedContext))

  def invoke(self, arg, from_tty):
    arg = arg.strip()
    if arg == "":
        # Backtrace all threadcontexts that are occupied in the current core
        maskAndCountPointer = gdb.parse_and_eval("Arachne::core.localOccupiedAndCount")
        if maskAndCountPointer == 0:
          print("Current core is not an Arachne core!")
          return
        bitmask = maskAndCountPointer.dereference()['_M_i']['occupied']

        # Perform a backtrace on all the occupied bits.
        for i in range(56):
           if (bitmask >> i) & 1:
               threadContext = gdb.parse_and_eval("Arachne::core.localThreadContexts[{0}]".format(i))
               print("Arachne Thread {0}: {1}".format(i, threadContext))
               try:
                   self.backtrace(threadContext, from_tty)
               except:
                   pass

        return

    # Verify that the type is correct
    typestring=str(gdb.parse_and_eval(arg).type)
    if typestring.strip() != "Arachne::ThreadContext *":
        print("Please pass an Arachne::ThreadContext*")
        return

    # Check if the provided threadcontext is NULL, and do nothing if it is.
    threadcontextvalue = int(gdb.parse_and_eval(arg))
    if threadcontextvalue == 0:
        print("A NULL pointer was passed!")
        return

    self.backtrace(arg, from_tty)

BackTraceArachneCommand()
