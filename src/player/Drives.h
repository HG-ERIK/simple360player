#pragma once
#include <string>
#include <vector>

// Titles only see their own folder (game:) until the console's drives are mounted.
struct Drive
{
    std::string  root;      // "hdd:\"
    std::wstring name;      // "Hard drive"
};

void               MountDrives();
std::vector<Drive> AvailableDrives();
