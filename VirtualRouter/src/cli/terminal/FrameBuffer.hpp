/**
 * @file FrameBuffer.hpp
 * @brief Diffed grid buffer for the prompt+input region of the console
 * @ingroup CLI_RUNTIME
 *
 * Holds the input region as a grid of cells and, on commit, emits only the
 * cells that differ from what is already on screen. Callers redraw the whole
 * line every keystroke and let the diff decide what actually reaches the
 * terminal, which keeps rendering correct without making every edit path
 * responsible for working out its own minimal update.
 */

#ifndef FRAME_BUFFER_HPP
#define FRAME_BUFFER_HPP

#include <string>
#include <vector>
#include <cstddef>
#include "ConsoleController.hpp"

namespace cli
{
/**
 * @struct Cell
 * @brief One character position on screen: what is drawn there and in what colour.
 *
 * Default-constructs to a plain space, which is what an untouched position
 * looks like, so a freshly cleared grid compares equal to blank screen.
 */
struct Cell
{
    char ch = ' ';
    Color color = Color::NONE;

    bool operator==(const Cell&) const = default;
    bool operator!=(const Cell&) const = default;
};

/**
 * @class FrameBuffer
 * @brief Double-buffered character grid that renders the input region by diffing.
 *
 * Two grids are kept: @c pending, which callers draw into, and @c prev, which
 * records what was last sent to the terminal. commit() compares them and emits
 * only the differences, along with the cursor movements needed to reach them.
 *
 * The buffer owns the input region only. Columns to the left of @ref origin on
 * the first row hold the prompt and are never written, so the prompt survives
 * even a full repaint.
 */
class FrameBuffer
{
public:
    /**
     * @brief Constructs a buffer for a terminal of the given width.
     *
     * @param w Terminal width in columns. Clamped to at least 1, since a
     *          zero-width grid has no addressable cells.
     */
    explicit FrameBuffer(size_t w)
        : width(w == 0 ? 1 : w)
    {}

    /**
     * @brief Changes the terminal width, discarding the current picture.
     *
     * A width change moves every cell after the first row, so nothing already
     * on screen can be trusted to line up. The grid is cleared and the buffer
     * marked dirty, making the next commit() a full repaint. A no-op if the
     * width is unchanged.
     *
     * @param w New terminal width in columns. Clamped to at least 1.
     */
    void setWidth(size_t w)
    {
        if (w == 0) w = 1;
        if (w == width) return;
        width = w;
        capacityRows = 0;
        pending.clear();
        prev.clear();
        physCursorRow = 0;
        physCursorCol = origin;
        dirty = true;
    }

    /**
     * @brief Returns the terminal width the grid is currently laid out for.
     *
     * @return Width in columns.
     */
    size_t getWidth() const { return width; }

    /**
     * @brief Starts a fresh line, forgetting whatever was on the previous one.
     *
     * Called when a new prompt is drawn. The cursor is assumed to be sitting at
     * @p originCol on the first row, just past the prompt, and the buffer is
     * marked dirty so the next commit() paints the new line in full.
     *
     * @param originCol Column where the prompt ends and the input region begins.
     */
    void reset(size_t originCol)
    {
        capacityRows = 0;
        pending.clear();
        prev.clear();
        physCursorRow = 0;
        physCursorCol = originCol;
        origin = originCol;
        dirty = true;
    }

    /**
     * @brief Begins a new frame, clearing the drawing grid for this render.
     *
     * The grid grows to fit @p neededRows and never shrinks, so a line that
     * wraps and is then shortened keeps enough rows to erase what it left
     * behind. Only @c pending is cleared; @c prev still describes the screen,
     * which is what lets commit() emit a diff rather than a repaint.
     *
     * @param neededRows Rows this frame needs. Clamped to at least 1.
     * @param originCol  Column where the prompt ends and the input region
     *                   begins. Clamped to the terminal width.
     */
    void beginFrame(size_t neededRows, size_t originCol)
    {
        if (neededRows == 0) neededRows = 1;
        if (neededRows > capacityRows) capacityRows = neededRows;
        origin = originCol < width ? originCol : width;
        pending.assign(capacityRows * width, Cell{});
    }

    /**
     * @brief Draws one character into the current frame.
     *
     * Out-of-range positions are ignored rather than treated as errors, so
     * callers can walk past the end of a wrapped line without bounds checks of
     * their own.
     *
     * @param row   Row within the grid, 0-based.
     * @param col   Column within the grid, 0-based.
     * @param c     Character to place.
     * @param color Colour to draw it in.
     */
    void set(size_t row, size_t col, char c, Color color = Color::NONE)
    {
        if (row >= capacityRows || col >= width) return;
        pending[row * width + col] = Cell{c, color};
    }

    /**
     * @brief Sends this frame to the terminal and leaves the cursor in place.
     *
     * Walks the grid, emits the cells that differ from what is on screen along
     * with the cursor movements needed to reach them, then moves the cursor to
     * the requested position. Everything goes out as a single print, so the
     * frame cannot be seen half-drawn. If nothing changed, nothing is printed.
     *
     * @param controller Terminal to write to.
     * @param cursorRow  Row to leave the cursor on, 0-based.
     * @param cursorCol  Column to leave the cursor on, 0-based.
     */
    void commit(ConsoleController& controller, size_t cursorRow, size_t cursorCol)
    {
        if (prev.size() < pending.size())
            prev.resize(pending.size(), Cell{});
        else if (prev.size() > pending.size())
        {
            prev.assign(pending.size(), Cell{});
            dirty = true;
        }
        size_t repaintEnd = 0;
        if (dirty)
        {
            for (size_t i = pending.size(); i-- > 0;)
            {
                if (pending[i] != Cell{})
                {
                    repaintEnd = i + 1;
                    break;
                }
            }
        }

        const size_t firstPaintable = origin;


        std::string out;
        out.reserve(pending.size());

        Color lastColor = Color::NONE;
        long physRow = static_cast<long>(physCursorRow);
        long physCol = static_cast<long>(physCursorCol);

        auto isPlain = [](Color c)
        {
            return c == Color::NONE || c == Color::TERMINAL || c == Color::PROMPT;
        };

        auto walkForward = [&](long row, long col) -> bool
        {
            if (row != physRow || col <= physCol || col - physCol > 4) return false;

            long probe = physCol;
            while (probe < col)
            {
                size_t idx = static_cast<size_t>(row) * width + static_cast<size_t>(probe);
                if (idx < firstPaintable || !isPlain(pending[idx].color)) return false;
                ++probe;
            }

            while (physCol < col)
            {
                size_t idx = static_cast<size_t>(row) * width + static_cast<size_t>(physCol);
                out += pending[idx].ch;
                ++physCol;
            }
            return true;
        };

        auto moveTo = [&](long row, long col)
        {
            if (walkForward(row, col)) return;

            long rowDelta = row - physRow;
            if (rowDelta > 0) out += "\033[" + std::to_string(rowDelta) + "B";
            else if (rowDelta < 0) out += "\033[" + std::to_string(-rowDelta) + "A";
            out += "\033[" + std::to_string(col + 1) + "G";
            physRow = row;
            physCol = col;
        };

        for (size_t r = 0; r < capacityRows; ++r)
        {
            for (size_t c = 0; c < width; ++c)
            {
                size_t idx = r * width + c;
                if (idx < firstPaintable) continue;

                const Cell& next = pending[idx];
                if (idx >= repaintEnd && next == prev[idx]) continue;

                bool contiguous = (static_cast<long>(r) == physRow) && (static_cast<long>(c) == physCol);
                if (!contiguous)
                    moveTo(static_cast<long>(r), static_cast<long>(c));

                if (next.color != lastColor)
                {
                    if (isPlain(next.color))
                    {
                        if (!isPlain(lastColor)) out += "\033[0m";
                    }
                    else
                    {
                        out += colorCode(next.color);
                    }
                    lastColor = next.color;
                }

                out += next.ch;
                ++physCol;
                if (static_cast<size_t>(physCol) >= width)
                {
                    physCol = 0;
                    ++physRow;
                }
            }
        }

        if (!isPlain(lastColor)) out += "\033[0m";

        if (static_cast<long>(cursorRow) != physRow || static_cast<long>(cursorCol) != physCol)
            moveTo(static_cast<long>(cursorRow), static_cast<long>(cursorCol));

        if (!out.empty())
            controller.print(out);

        physCursorRow = static_cast<size_t>(physRow);
        physCursorCol = static_cast<size_t>(physCol);
        prev.swap(pending);
        dirty = false;
    }

private:
    static std::string colorCode(Color c)
    {
        switch (c)
        {
            case Color::BLACK:   return "\033[1;30m";
            case Color::RED:     return "\033[1;31m";
            case Color::GREEN:   return "\033[1;32m";
            case Color::YELLOW:  return "\033[1;33m";
            case Color::BLUE:    return "\033[1;34m";
            case Color::MAGENTA: return "\033[1;35m";
            case Color::CYAN:    return "\033[1;36m";
            case Color::WHITE:   return "\033[1;37m";
            default:             return "";
        }
    }

    size_t width;
    size_t capacityRows = 0;
    std::vector<Cell> pending;
    std::vector<Cell> prev;

    size_t physCursorRow = 0;
    size_t physCursorCol = 0;
    size_t origin = 0; ///< First column of row 0 this buffer owns; everything left of it is the prompt.

    bool dirty = true; ///< prev holds no valid picture of the screen; the next commit repaints every content cell.
};
}

#endif // FRAME_BUFFER_HPP