#pragma once

// Math Quiz on screen. Freestanding builders over plain models: no renderer, no
// Activity, no storage. The activity binds the faces (mathQuizFaces) and fills
// the models; everything a finger can hit is registered here, from the same
// rects the pixels were drawn from.
//
// Faces: SMALL is the button cut for labels, BODY the 44px cut for sums and
// answers, TITLE the display cut for the header and headings.

#include "../ui/ToyboxScreen.h"
#include "MathQuizCore.h"

namespace mathquizui {

namespace fui = freeink::ui;

enum : fui::ActionId {
  ActionGrade = 1,   // value: the class, 1..6
  ActionScores = 2,  // the charts, without playing first
  ActionChoice = 3,  // value: which of the four
  ActionNext = 4,
  ActionEnd = 5,
  ActionOlder = 6,
  ActionNewer = 7,
  ActionPractise = 8,
};

struct MenuModel {
  int grade = 0;  // the class picked last time, 0 for none
  // One sample sum per class, so a child who is unsure can pick by what the
  // questions look like.
  const char* sample[mathquiz::kGrades] = {};
};

struct QuestionModel {
  int grade = 1;
  const mathquiz::Question* question = nullptr;
  int chosen = -1;  // -1 until answered
  int asked = 0;    // this round, including the one on screen once answered
  int right = 0;
  int tenthsSec = 0;  // how long this answer took, shown once answered
};

struct ResultsModel {
  const mathquiz::Window* window = nullptr;  // null when the clock is not set
  int page = 0;
  // This round, when the screen follows one. asked 0 hides the line.
  int asked = 0;
  int right = 0;
  int tenthsSec = 0;
};

void buildMenu(toybox::Screen& screen, const MenuModel& model);
void buildQuestion(toybox::Screen& screen, const QuestionModel& model);
void buildResults(toybox::Screen& screen, const ResultsModel& model);

}  // namespace mathquizui
