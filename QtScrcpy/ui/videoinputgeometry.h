#ifndef VIDEOINPUTGEOMETRY_H
#define VIDEOINPUTGEOMETRY_H
#include <QWidget>
namespace VideoInputGeometry {
inline bool contains(QWidget *receiver, QWidget *surface, const QPoint &point) {
    return surface && surface->rect().contains(surface->mapFrom(receiver, point));
}
}
#endif
