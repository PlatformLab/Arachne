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

  def backtrace(self, threadContext, from_tty):

    # Check if we are backtracing the current context
    loadedContext = gdb.parse_and_eval("Arachne::loadedContext")
    if isinstance(threadContext, basestring):
        threadContext = gdb.parse_and_eval(threadContext)
    if long(loadedContext) == long(threadContext):
        gdb.execute("backtrace", from_tty)
        return

    originalSP = gdb.parse_and_eval("$sp")
    originalPC = long(gdb.parse_and_eval("$pc"))
    print "Original SP:", originalSP
    print "Original PC:", originalPC
    gdb.execute("set $rsp=((Arachne::ThreadContext*){0})->sp + Arachne::SpaceForSavedRegisters".format(threadContext), from_tty)
    gdb.execute("set $pc = *(void **)$rsp", from_tty)
    gdb.execute("backtrace", from_tty)
    gdb.execute("set ($sp)={0}".format(originalSP), from_tty)
    gdb.execute("set ($pc)={0}".format(originalPC), from_tty)

  def invoke(self, arg, from_tty):
    arg = arg.strip()
    if arg == "":
        # Backtrace all threadcontexts that are occupied in the current core
        maskAndCountPointer = gdb.parse_and_eval("Arachne::localOccupiedAndCount")
        if maskAndCountPointer == 0:
          print "Current core is not an Arachne core!"
          return
        bitmask = maskAndCountPointer.dereference()['_M_i']['occupied']

        # Perform a backtrace on all the occupied bits.
        for i in xrange(56):
           if (bitmask >> i) & 1:
               threadContext = gdb.parse_and_eval("Arachne::localThreadContexts[{0}]".format(i))
               print "Arachne Thread {0}: {1}".format(i, threadContext)
               try:
                   self.backtrace(threadContext, from_tty)
               except:
                   pass

        return

    # Verify that the type is correct
    typestring=str(gdb.parse_and_eval(arg).type)
    if typestring.strip() != "Arachne::ThreadContext *":
        print "Please pass an Arachne::ThreadContext*"
        return

    # Check if the provided threadcontext is NULL, and do nothing if it is.
    threadcontextvalue = long(gdb.parse_and_eval(arg))
    if threadcontextvalue == 0:
        print "A NULL pointer was passed!"
        return

    self.backtrace(arg, from_tty)

BackTraceArachneCommand()
