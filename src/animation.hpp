#pragma once

#include <Geode/Geode.hpp>
#include <Geode/utils/random.hpp>
#include <chrono>
#include <cmath>

namespace smoothtextinput {

constexpr float kDistanceMax = 50.0f;
constexpr int   kFadeInTag   = 0x7AD0;
constexpr int   kGhostTag    = 0x7AD1;

struct PopSettings {
    bool  enabled;
    int   angle;
    bool  randomAngle;
    float distance;
    bool  randomDistance;
};

struct Ghost {
    cocos2d::CCTexture2D* tex;
    cocos2d::CCRect       rect;
    cocos2d::CCPoint      pos;
    cocos2d::CCPoint      anchor;
    float                 scaleX;
    float                 scaleY;
    float                 rotation;
    cocos2d::ccColor3B    color;
    GLubyte               opacity;
};

struct PendingFade {
    std::chrono::steady_clock::time_point startTime;
    float                                 duration;
    cocos2d::CCPoint                      motion;
};

struct AnimSettings {
    bool        enabled;
    float       fadeIn;
    float       fadeOut;
    PopSettings popIn;
    PopSettings popOut;
};

inline AnimSettings loadConfig() {
    auto* m = geode::Mod::get();
    return {
        m->getSettingValue<bool>("enabled"),
        static_cast<float>(m->getSettingValue<double>("fade-in-duration")),
        static_cast<float>(m->getSettingValue<double>("fade-out-duration")),
        {
            m->getSettingValue<bool>("pop-in-enabled"),
            static_cast<int>(m->getSettingValue<int64_t>("pop-in-angle")),
            m->getSettingValue<bool>("pop-in-angle-random"),
            static_cast<float>(m->getSettingValue<double>("pop-in-distance")),
            m->getSettingValue<bool>("pop-in-distance-random"),
        },
        {
            m->getSettingValue<bool>("pop-out-enabled"),
            static_cast<int>(m->getSettingValue<int64_t>("pop-out-angle")),
            m->getSettingValue<bool>("pop-out-angle-random"),
            static_cast<float>(m->getSettingValue<double>("pop-out-distance")),
            m->getSettingValue<bool>("pop-out-distance-random"),
        },
    };
}

inline bool animationsActive(AnimSettings const& s) {
    if (!s.enabled) return false;
    if (s.fadeIn > 0.f || s.fadeOut > 0.f) return true;
    return s.popIn.enabled || s.popOut.enabled;
}

inline cocos2d::CCPoint popMotion(float angleDeg, float distance) {
    float r = angleDeg * M_PI / 180.0f; // I'm sorry dankmeme, you're right.
    return cocos2d::CCPoint(std::sin(r) * distance, std::cos(r) * distance);
}

inline void resolvePop(PopSettings const& settings, float& outAngle, float& outDist) {
    if (!settings.enabled) { outAngle = 0.0f; outDist = 0.0f; return; }
    outAngle = settings.randomAngle
        ? geode::utils::random::generate<float>(0.0f, 360.0f)
        : static_cast<float>(settings.angle);
    outDist  = settings.randomDistance
        ? geode::utils::random::generate<float>(0.0f, kDistanceMax)
        : settings.distance;
}

} // namespace smoothtextinput
