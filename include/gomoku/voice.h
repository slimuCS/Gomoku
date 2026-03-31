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

class voice {
public:
    explicit voice() = default;

    ~voice() = default;

    bool initAudioSystem();
    void cleanupAudioSystem();

    void clickSound() const;
    void backGroundMusic() const;
    void placeStoneSound() const;
    void stopBackgroundMusic() const;
private:
};


#endif // voice_H