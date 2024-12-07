#ifndef _INCLUDE_ARGS_H_
#define _INCLUDE_ARGS_H_

#include <string>

namespace Args {

  enum class Command {
    Help,
    Daemon,
    Check,

    Reset,
    Load,
    Save,

    Status,

    List,
    View,
    Connect,
    Disconnect,

    ConnectionLogicTest,
  };
  extern Command command;

  // Check command options
  extern std::string rulesFilePath;

  // Reset command options
  extern bool keepObserved;
  extern bool resetHard;

  // List command options
  extern bool listClients;
  extern bool listPorts;
  extern bool listConnections;

  extern bool listAll;
  extern bool listPlain;
  extern bool listDetails;

  extern bool listNumericSort;

  // Connect and Disconnect args
  extern std::string portSender;
  extern std::string portDest;

  extern int exitCode;
  bool parse(int argc, char* argv[]);
}

#endif // _INCLUDE_ARGS_H_
