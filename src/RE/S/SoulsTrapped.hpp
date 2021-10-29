//

// From PowerOfThree's decode:
//     https://github.com/powerof3/CommonLibSSE/blob/dfa73e501c0ced55b8ce6732edc9ea4c531e9229/include/RE/S/SoulsTrapped.h

#pragma once

#include "RE/B/BSTEvent.h"

namespace RE {
    class Actor;

    struct SoulsTrapped {
    public:
        struct Event {
        public:
            // members
            Actor* trapper; // 00
            Actor* target;  // 08
        };
        static_assert(sizeof(Event) == 0x10);

        // clang-format off
        static BSTEventSource<SoulsTrapped::Event>* GetEventSource();
        static void                                 SendEvent(Actor* a_trapper, Actor* a_target);
        // clang-format on
    };
}