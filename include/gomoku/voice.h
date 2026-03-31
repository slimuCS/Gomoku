/**
 * @file voice.h
 * @author shawn
 * @date 2026/3/31
 * @brief 
 *
 * * Under the hood:
 * - Memory Layout: 
 * - System Calls / Interactions: 
 * - Resource Impact: 
 */
#ifndef voice_H
#define voice_H

namespace gomoku {
    class voice {
    public:
        explicit voice() = default;

        ~voice() = default;

        void clickSound() const;
        void backGroundMusic() const;
        void placeStoneSound() const;

    private:
    };
}

#endif // voice_H