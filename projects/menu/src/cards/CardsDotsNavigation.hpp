#pragma once

#include <nxui/core/Input.hpp>
#include <algorithm>

namespace cards {

// The page chips form the first row; action rows may end before column four.
inline bool navigateDots(const nxui::Input& input, int pages, int tiles, int& row, int& col) {
    constexpr int columns = 4;
    const int rows = 1 + (tiles + columns - 1) / columns;
    const int oldRow = row, oldCol = col;
    using B = nxui::Button;
    if (input.isDown(B::DUp) || input.isDown(B::LStickU))
        row = std::max(0, row - 1);
    else if (input.isDown(B::DDown) || input.isDown(B::LStickD))
        row = std::min(rows - 1, row + 1);
    const int count = row == 0 ? pages : std::min(columns, tiles - (row - 1) * columns);
    col = std::clamp(col, 0, std::max(0, count - 1));
    if (input.isDown(B::DLeft) || input.isDown(B::LStickL))
        col = std::max(0, col - 1);
    else if (input.isDown(B::DRight) || input.isDown(B::LStickR))
        col = std::min(std::max(0, count - 1), col + 1);
    return oldRow != row || oldCol != col;
}

} // namespace cards
