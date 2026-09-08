/**
 * @file home_screen.hpp
 * @brief Home screen: the app icons, laid out like the original
 */

#pragma once

#include <functional>

namespace home_screen {

/** @brief Apps on the home screen, in the order the original shows them */
enum class App {
    Climate,
    Speakers,
    Weather,
    Lights,
    Energy,
    Timer,
    Moods,
};

/** @brief Called when an icon is tapped */
using LaunchCallback = std::function<void(App app)>;

/**
 * @brief Build the home screen
 * @param callback Told which app was tapped
 */
void build(LaunchCallback callback);

/** @brief Make the home screen the active one */
void show();

}  // namespace home_screen
