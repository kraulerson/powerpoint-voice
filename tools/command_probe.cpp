// command_probe — a headless dev/UAT utility: read phrases and print the voice
// command each maps to. Lets a human exercise the F2/F3 command grammar (and F4's
// number parsing) by TYPING phrases, before the microphone/speech engine exists.
//
//   command_probe "next slide" "go to slide fifteen" "so let's move on"
//   echo "pause presentation" | command_probe        # one phrase per stdin line
//
// Purely local; reads no files and captures no audio.
#include <iostream>
#include <optional>
#include <string>

#include <QString>

#include "command/command_matcher.hpp"

using namespace pptv;

namespace {

const char* typeName(CommandType t) {
    switch (t) {
    case CommandType::NextSlide:
        return "NextSlide";
    case CommandType::PreviousSlide:
        return "PreviousSlide";
    case CommandType::PausePresentation:
        return "PausePresentation";
    case CommandType::ContinuePresentation:
        return "ContinuePresentation";
    case CommandType::GoToSlide:
        return "GoToSlide";
    }
    return "?";
}

void probe(const QString& phrase) {
    const std::optional<Command> c = matchCommand(phrase);
    std::cout << "  \"" << phrase.toStdString() << "\"  ->  ";
    if (!c) {
        std::cout << "(no command)\n";
    } else if (c->type == CommandType::GoToSlide) {
        std::cout << "GoToSlide(" << c->slideNumber << ")\n";
    } else {
        std::cout << typeName(c->type) << "\n";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        for (int i = 1; i < argc; ++i) {
            probe(QString::fromLocal8Bit(argv[i]));
        }
    } else {
        std::string line;
        while (std::getline(std::cin, line)) {
            probe(QString::fromStdString(line));
        }
    }
    return 0;
}
