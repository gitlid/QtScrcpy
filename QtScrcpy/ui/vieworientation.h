#ifndef QSC_VIEWORIENTATION_H
#define QSC_VIEWORIENTATION_H
#include <QSize>

// Desktop-only presentation policy. The source size always remains the real
// decoder/controller size; a user preference is never an accumulated rotation.
class ViewOrientation {
public:
    enum Mode { FollowPhone = 0, KeepLandscape = 1, KeepPortrait = 2 };
    static int normalized(int turns) { return (turns % 4 + 4) % 4; }
    bool setSourceSize(const QSize &size) {
        if (size.isEmpty()) return false;
        const bool changed = m_source != size;
        m_source = size;
        // A square transition contains no orientation information. Keep the
        // last known source axis instead of flipping the selected preference.
        if (size.width() != size.height()) m_sourceLandscape = size.width() > size.height();
        return changed;
    }
    QSize sourceSize() const { return m_source; }
    Mode mode() const { return m_mode; }
    bool locked() const { return m_mode != FollowPhone; }
    int rotation() const {
        if (!locked()) return 0;
        const bool sameAxis = m_sourceLandscape == (m_mode == KeepLandscape);
        return sameAxis ? m_alignedTurns : m_crossTurns;
    }
    QSize viewSize() const {
        return rotation() % 2 ? QSize(m_source.height(), m_source.width()) : m_source;
    }
    void setMode(Mode mode) {
        if (mode != FollowPhone && mode != KeepLandscape && mode != KeepPortrait) return;
        m_mode = mode;
        // Explicit 'keep landscape/portrait' means native matching content is
        // upright. The opposite source axis uses the chosen quarter-turn side.
        m_alignedTurns = 0;
        if (mode == FollowPhone) m_crossTurns = 1;
    }
    bool selectRotation(int turns) {
        if (m_source.isEmpty()) return false;
        turns = normalized(turns);
        const int delta = normalized(turns - rotation());
        if (locked() && delta == 2) {
            // An explicit 180-degree flip applies on either source axis.
            m_alignedTurns = normalized(m_alignedTurns + 2);
            m_crossTurns = normalized(m_crossTurns + 2);
            return true;
        }
        const bool landscape = m_sourceLandscape != bool(turns % 2);
        m_mode = landscape ? KeepLandscape : KeepPortrait;
        if (turns % 2) {
            m_crossTurns = turns;
            m_alignedTurns = 0;
        } else {
            m_alignedTurns = turns;
            m_crossTurns = normalized(turns + (delta == 3 ? 3 : 1));
        }
        return true;
    }
private:
    QSize m_source;
    Mode m_mode = FollowPhone;
    bool m_sourceLandscape = false;
    int m_alignedTurns = 0;
    int m_crossTurns = 1;
};
#endif
