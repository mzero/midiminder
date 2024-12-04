#include "client.h"

#include <fmt/format.h>

#include "seqsnapshot.h"
#include "term.h"


namespace {
  template<typename T>
  void forwardSelection(
      std::size_t& selection,
      const std::vector<T>& items,
      bool (T::*filter)() const)
  {
      size_t i = selection;
      while (true) {
        i += 1;
        if (i >= items.size()) return;
        if ((items[i].*filter)()) {
          selection = i;
          return;
        }
      }
  }

  template<typename T>
  void backwardSelection(
      std::size_t& selection,
      const std::vector<T>& items,
      bool (T::*filter)() const)
  {
      size_t i = selection;
      while (true) {
        if (i == 0) return;
        i -= 1;
        if ((items[i].*filter)()) {
          selection = i;
          return;
        }
      }
  }

  constexpr bool utf8 = true;
  constexpr const char* boxVertical     = utf8 ? "\xe2\x94\x82" : "|"; // U+2502
  constexpr const char* boxHorizontal   = utf8 ? "\xe2\x94\x80" : "-"; // U+2500
  constexpr const char* boxCornerTL     = utf8 ? "\xe2\x94\x8c" : "+"; // U+250c
  constexpr const char* boxCornerTR     = utf8 ? "\xe2\x94\x90" : "+"; // U+2510
  constexpr const char* boxCornerBL     = utf8 ? "\xe2\x94\x94" : "+"; // U+2514
  constexpr const char* boxCornerBR     = utf8 ? "\xe2\x94\x98" : "+"; // U+2518

  enum class Mode {
    Menu,

    PickSender,
    PickDest,
    ConfirmConnection,

    PickConnection,
    ConfirmDisconnection,

    Quit,
  };

  struct View {
    Term& term;
    SeqSnapshot seqState;

    View(Term& t) : term(t) { }

    std::size_t topRowPorts;
    std::size_t topRowConnections;
    std::size_t topRowPrompt;

    void layout();

    std::size_t selectedSender;
    std::size_t selectedDest;
    std::size_t selectedConnection;

    std::string message;
    void setMessage(const std::string&);

    bool dirtyPorts = false;
    bool dirtyConnections = false;
    bool dirtyPrompt = false;

    void drawPorts();
    void drawConnections();
    void drawPrompt();

    void render();


    Mode mode;
    Mode priorMode();
    void gotoMode(Mode);

    void handleEvent(const Term::Event& ev);
    bool handleGlobalEvent(const Term::Event& ev);
    bool handleMenuEvent(const Term::Event& ev);
    bool handlePortPickerEvent(const Term::Event& ev);
    bool handleConnectionPickerEvent(const Term::Event& ev);
    bool handleConfirmEvent(const Term::Event& ev);

    void debugMessage(const std::string&);
    void run();
  };


  void View::layout() {
    auto portsHeight       = 2 + seqState.ports.size() + 1;
    auto connectionsHeight = 2 + seqState.connections.size() + 1;
    auto promptHeight      = 2;

    topRowPrompt = term.rows() + 1 - promptHeight;
    topRowConnections = topRowPrompt - connectionsHeight;
    topRowPorts = topRowConnections - portsHeight;

    dirtyPorts = true;
    dirtyConnections = true;
    dirtyPrompt = true;

    term.clearDisplay();
  }

  void View::render() {
    if (dirtyPorts)       drawPorts();
    if (dirtyConnections) drawConnections();
    if (dirtyPrompt)      drawPrompt();

    std::cout.flush();
    dirtyPorts = dirtyConnections = dirtyPrompt = false;
  }

  void View::drawPorts() {
    bool picking = false;
    bool confirming = false;
    size_t s1 = seqState.ports.size();
    size_t s2 = seqState.ports.size();
    size_t inv = seqState.ports.size();
    auto func = &Address::canBeSender;

    switch (mode) {
      case Mode::PickSender:
        picking = true;
        s1 = selectedSender;
        inv = selectedSender;
        break;
      case Mode::PickDest:
        picking = true;
        s1 = selectedSender;
        s2 = selectedDest;
        inv = selectedDest;
        func = &Address::canBeDest;
        break;
      case Mode::ConfirmConnection:
        confirming = true;
        s1 = selectedSender;
        s2 = selectedDest;
        break;
      default:
        break;
    }
    int y = topRowPorts;

    term.clearLine(y++);
    std::cout << boxCornerTL << boxHorizontal << " Ports ";
    for (auto i = seqState.clientWidth + seqState.portWidth + 15; i > 0; --i)
      std::cout << boxHorizontal;
    term.clearLine(y++);
    std::cout << boxVertical;

    char label = 'a';
    size_t index = 0;
    for (auto& p : seqState.ports) {
      term.clearLine(y);
      std::cout << boxVertical << ' ';

      bool isS1 = index == s1;
      bool isS2 = index == s2;
      if (index == inv)                     std::cout << Term::Style::inverse;
      else if (isS1 || isS2)                std::cout << Term::Style::bold;
      else if (confirming)                  std::cout << Term::Style::dim;
      else if (picking && !(p.*func)())     std::cout << Term::Style::dim;

      if (picking) std::cout << label << ')';
      else         std::cout << "  ";

      fmt::print(" {:{cw}} : {:{pw}} [{:3}:{}] {}{}",
        p.client, p.port, p.addr.client, p.addr.port,
        (isS1 | isS2) ? "    " : "",  // shifts over the arrow is selected
        (isS1 | isS2) ? SeqSnapshot::dirStr(isS1, isS2)
                      : SeqSnapshot::addressDirStr(p),
        fmt::arg("cw", seqState.clientWidth), fmt::arg("pw", seqState.portWidth));

      y++, label++, index++;
    }
  }

  void View::drawConnections() {
    bool picking = mode == Mode::PickConnection;
    bool confirming = mode == Mode::ConfirmDisconnection;

    int y = topRowConnections;

    term.clearLine(y++);
    std::cout << boxCornerTL << boxHorizontal << " Connections ";
    for (auto i = 2*(seqState.clientWidth + seqState.portWidth) - 4; i > 0; --i)
      std::cout << boxHorizontal;
    term.clearLine(y++);
    std::cout << boxVertical;

    char label = 'a';
    size_t index = 0;

    for (auto& c : seqState.connections) {
      term.clearLine(y);
      std::cout << boxVertical << ' ';

      if (picking && index == selectedConnection)
                                          std::cout << Term::Style::inverse;
      if (confirming) {
        if (index == selectedConnection)  std::cout << Term::Style::bold;
        else                              std::cout << Term::Style::dim;
      }
      fmt::print("{}{} {} --> {}",
        picking ? label : ' ',
        picking ? ')' : ' ',
        c.sender, c.dest);

      y++, label++, index++;
    }
  }

  void View::drawPrompt() {
    const char* line1 = "";

    switch (mode) {
      case Mode::Menu:
        line1 = "Q)uit, C)connect, D)isconnect";
        break;

      case Mode::PickSender:
        line1 = "Use arrows to pick a sender and hit return, or type a letter";
        break;

      case Mode::PickDest:
        line1 = "Now pick a dest the same way";
        break;

      case Mode::ConfirmConnection:
        line1 = "Confirm with return, or cancel with ESC";
        break;

      case Mode::PickConnection: {
        line1 = "Use arrows to pick a connection and hit return, or type a letter";
        break;
      }

      case Mode::ConfirmDisconnection:{
        line1 = "Confirm with return, or cancel with ESC";
        break;
      }

      case Mode::Quit:
        line1 = "Quitting...";
        break;
    }
    term.clearLine(topRowPrompt);
    std::cout << "  >> " << line1;
    term.clearLine(topRowPrompt + 1);
    if (message.length() > 0)
      std::cout << "  ** " << message;
    term.moveCursor(topRowPrompt, 1);
  }

  void View::setMessage(const std::string& s) {
    message = s;
    dirtyPrompt = true;
  }

  void View::gotoMode(Mode newMode) {
    Mode modes[] = {mode, newMode};
    for (auto m : modes) {
      switch(m) {
        case Mode::Menu:                  break;
        case Mode::PickSender:
        case Mode::PickDest:
        case Mode::ConfirmConnection:     dirtyPorts = true; break;
        case Mode::PickConnection:
        case Mode::ConfirmDisconnection:  dirtyConnections = true; break;
        case Mode::Quit:                  break;
      }
    }
    dirtyPrompt = true;
    mode = newMode;
  }

  Mode View::priorMode() {
    switch (mode) {
      case Mode::Menu:                    return Mode::Menu;
      case Mode::PickSender:              return Mode::Menu;
      case Mode::PickDest:                return Mode::PickSender;
      case Mode::ConfirmConnection:       return Mode::PickDest;
      case Mode::PickConnection:          return Mode::Menu;
      case Mode::ConfirmDisconnection:    return Mode::PickConnection;
      case Mode::Quit:                    return Mode::Quit;
    }
    return mode; // never reached, appeases the compiler
  }

  bool View::handleGlobalEvent(const Term::Event& ev) {
    switch (ev.type) {
      case Term::EventType::Char: {
        switch (ev.character) {
          case '\x03':      // control-c
            gotoMode(Mode::Quit);
            return true;

          case '\x0c':      // control-l  - like in vi!
            layout();
            return true;

          case '\x1b':      // escape
            gotoMode(Mode::Menu);
            return true;

          default:
            break;
        }
        break;
      }

      case Term::EventType::Key: {
        switch (ev.key) {
          case Term::Key::Left:
            gotoMode(priorMode());
            return true;
          default:
            break;
        }
        break;
      }

      case Term::EventType::Resize: {
        layout();
        return true;
      }

      default:
        break;
    }
    return false;
  }

  bool View::handleMenuEvent(const Term::Event& ev) {
    if (ev.type == Term::EventType::Char) {
      switch (ev.character) {
        case 'C':
        case 'c':
          gotoMode(Mode::PickSender);
          dirtyPrompt = dirtyPorts = true;
          return true;

        case 'D':
        case 'd':
        case 'X':
        case 'x':
          gotoMode(Mode::PickConnection);
          dirtyPrompt = dirtyConnections = true;
          return true;

        case 'Q':
        case 'q':
          gotoMode(Mode::Quit);
          return true;

        case 'R':
        case 'r':     // hidden command, should never be needed
          seqState.refresh();
          layout();
          return true;
      }
    }
    return false;
  }

  bool View::handlePortPickerEvent(const Term::Event& ev) {
    std::size_t* selector;
    using FilterFunc = bool (Address::*)() const;
    FilterFunc filter;
    const char* typeString;
    Mode nextMode;

    switch (mode) {
      case Mode::PickSender:
        selector = &selectedSender;
        filter = &Address::canBeSender;
        typeString = "sender";
        nextMode = Mode::PickDest;
        break;

      case Mode::PickDest:
        selector = &selectedDest;
        filter = &Address::canBeDest;
        typeString = "destination";
        nextMode = Mode::ConfirmConnection;
        break;

      default:
        return false;
    }

    dirtyPorts = true;

    switch (ev.type) {
      case Term::EventType::Char: {
        auto& c = ev.character;

        size_t pick;
        if ('A' <= c && c <= 'Z')       pick = c - 'A';
        else if ('a' <= c && c <= 'z')  pick = c - 'a';
        else if (ev.character == '\r'
              || ev.character == '\t')  pick = *selector;
        else                            return false;

        if (pick < seqState.ports.size()) {
          if ((seqState.ports[pick].*filter)()) {
            *selector = pick;
            gotoMode(nextMode);
            return true;
          }
          else {
            setMessage(fmt::format("That port cannot be a {}", typeString));
          }
        }

        return false;
      }

      case Term::EventType::Key: {
        switch (ev.key) {
          case Term::Key::Down:
            forwardSelection(*selector, seqState.ports, filter);
            break;
          case Term::Key::Up:
            backwardSelection(*selector, seqState.ports, filter);
            break;
          default:
            return false;
        }
        return true;
      }

      default:
        break;
    }

    return false;
  }

 bool View::handleConnectionPickerEvent(const Term::Event& ev) {
    dirtyConnections = true;

    switch (ev.type) {
      case Term::EventType::Char: {
        auto& c = ev.character;

        size_t pick;
        if ('A' <= c && c <= 'Z')       pick = c - 'A';
        else if ('a' <= c && c <= 'z')  pick = c - 'a';
        else if (ev.character == '\r'
              || ev.character == '\t')  pick = selectedConnection;
        else                            return false;

        if (pick < seqState.connections.size()) {
          selectedConnection = pick;
          gotoMode(Mode::ConfirmDisconnection);
          return true;
        }
        return false;
      }

      case Term::EventType::Key: {
        size_t pick = selectedConnection;
        switch (ev.key) {
          case Term::Key::Down:                 pick += 1;  break;
          case Term::Key::Up:     if (pick > 0) pick -= 1;  break;
          default:
            return false;
        }
        if (pick >= seqState.connections.size())
          pick = seqState.connections.size() - 1;
        selectedConnection = pick;
        return true;
      }

      default:
        break;
    }

    return false;
  }

  bool View::handleConfirmEvent(const Term::Event& ev) {
    if (ev.type == Term::EventType::Char) {
      if (ev.character == '\r' || ev.character == '\t') {

        switch (mode) {
          case Mode::ConfirmConnection: {
            setMessage(fmt::format("Would connect {} --> {}",
              seqState.ports[selectedSender],
              seqState.ports[selectedDest]));
            break;
          }

          case Mode::ConfirmDisconnection: {
            auto& conn = seqState.connections[selectedConnection];
            setMessage(fmt::format("Would disconnect {} -x-> {}",
              conn.sender, conn.dest));
            break;
          }

          default:
            return false;
        }
        gotoMode(Mode::Menu);
        seqState.refresh();   // FIXME: remove once Seq events are handled
        layout();
        return true;
      }
    }
    return false;
  }

  void View::handleEvent(const Term::Event& ev) {
    bool handled = false;

    if (handleGlobalEvent(ev)) {
      if (handled) debugMessage("handled in handleGlobalEvent");
      return;
    }

    switch (mode) {
      case Mode::Menu:
        handled = handleMenuEvent(ev);
        if (handled) debugMessage("handled in handleMenuEvent");
        break;

      case Mode::PickSender:
      case Mode::PickDest:
        handled = handlePortPickerEvent(ev);
        if (handled) debugMessage("handled in handlePortPickerEvent");
        break;

      case Mode::PickConnection:
        handled = handleConnectionPickerEvent(ev);
        if (handled) debugMessage("handled in handleConnectionPickerEvent");
        break;

      case Mode::ConfirmConnection:
      case Mode::ConfirmDisconnection:
        handled = handleConfirmEvent(ev);
        if (handled) debugMessage("handled in handleConfirmEvent");
        break;

      case Mode::Quit:
        return;
    }

    if (!handled) {
      switch (ev.type) {
        case Term::EventType::Char:
          debugMessage(fmt::format("Unassigned character {:?}", ev.character));
          break;

        case Term::EventType::Key:
          debugMessage(fmt::format("Unassigned key {}", static_cast<int>(ev.key)));
          break;

        case Term::EventType::UnknownEscapeSequence:
          debugMessage(fmt::format("Unknown escape sequence: {:?}", ev.unknown));
          break;

        default:
          debugMessage(fmt::format("Unknown event type {}", int(ev.type)));
          break;
      }
    }
  }

  void View::debugMessage(const std::string& s) {
    const size_t debugAreaHeight = 15;
    static size_t row = 1;
    static int messageNumber = 1;

    term.clearLine(row);
    fmt::print("{:3}: {}", messageNumber, s);
    row = (row  % debugAreaHeight) + 1;
    term.clearLine(row); // clear the next line so the current one stands out
    messageNumber += 1;
  }

  void View::run() {
    mode = Mode::Menu;
    selectedSender = 0;
    selectedDest = 0;
    selectedConnection = 0;

    layout();

    while (mode != Mode::Quit) {
      render();

      Term::Event ev = term.getEvent();
      if (ev.type == Term::EventType::None) continue;

      setMessage("");
      handleEvent(ev);
      debugMessage(fmt::format("in mode {}", int(mode)));
    }

  }
}

namespace Client {

  void viewCommand() {
    Term term;

    if(!term.good()) {
      listCommand();
      return;
    }

    View view(term);
    view.run();
  }

}