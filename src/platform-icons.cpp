/*
obs-unified-chat
Copyright (C) 2026 ebehar

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "platform-icons.hpp"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>

namespace unified_chat {

static void PaintTwitch(QPainter &p, qreal s)
{
	// Purple speech bubble with two "eyes", after the Twitch glitch mark.
	QPainterPath bubble;
	bubble.moveTo(0.18 * s, 0.08 * s);
	bubble.lineTo(0.90 * s, 0.08 * s);
	bubble.lineTo(0.90 * s, 0.58 * s);
	bubble.lineTo(0.66 * s, 0.80 * s);
	bubble.lineTo(0.48 * s, 0.80 * s);
	bubble.lineTo(0.32 * s, 0.95 * s);
	bubble.lineTo(0.32 * s, 0.80 * s);
	bubble.lineTo(0.10 * s, 0.80 * s);
	bubble.lineTo(0.10 * s, 0.22 * s);
	bubble.closeSubpath();
	p.fillPath(bubble, QColor(0x91, 0x46, 0xff));

	p.setPen(Qt::NoPen);
	p.setBrush(Qt::white);
	p.drawRect(QRectF(0.44 * s, 0.25 * s, 0.09 * s, 0.24 * s));
	p.drawRect(QRectF(0.66 * s, 0.25 * s, 0.09 * s, 0.24 * s));
}

static void PaintYouTube(QPainter &p, qreal s)
{
	// Red rounded rectangle with a white play triangle.
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(0xff, 0x00, 0x33));
	p.drawRoundedRect(QRectF(0.02 * s, 0.17 * s, 0.96 * s, 0.66 * s), 0.18 * s, 0.18 * s);

	QPainterPath play;
	play.moveTo(0.40 * s, 0.34 * s);
	play.lineTo(0.68 * s, 0.50 * s);
	play.lineTo(0.40 * s, 0.66 * s);
	play.closeSubpath();
	p.fillPath(play, Qt::white);
}

QImage PlatformImage(Platform platform, int size, qreal devicePixelRatio)
{
	const int pixels = qMax(1, qRound(size * devicePixelRatio));
	QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);

	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	if (platform == Platform::Twitch)
		PaintTwitch(p, pixels);
	else
		PaintYouTube(p, pixels);
	p.end();

	image.setDevicePixelRatio(devicePixelRatio);
	return image;
}

QImage SelfBadgeImage(int size, qreal devicePixelRatio)
{
	const int pixels = qMax(1, qRound(size * devicePixelRatio));
	QImage image(pixels, pixels, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);

	// Gold five-point star, point up.
	const qreal center = pixels / 2.0;
	const qreal outer = pixels * 0.48;
	const qreal inner = outer * 0.42;
	QPainterPath star;
	for (int i = 0; i < 10; ++i) {
		const qreal radius = i % 2 ? inner : outer;
		const qreal angle = qDegreesToRadians(-90.0 + i * 36.0);
		const QPointF point(center + radius * qCos(angle), center + radius * qSin(angle));
		if (i == 0)
			star.moveTo(point);
		else
			star.lineTo(point);
	}
	star.closeSubpath();

	QPainter p(&image);
	p.setRenderHint(QPainter::Antialiasing);
	p.fillPath(star, QColor(0xff, 0xd6, 0x00));
	p.end();

	image.setDevicePixelRatio(devicePixelRatio);
	return image;
}

QIcon PlatformIcon(Platform platform, int size)
{
	QIcon icon;
	for (qreal dpr : {1.0, 1.5, 2.0})
		icon.addPixmap(QPixmap::fromImage(PlatformImage(platform, size, dpr)));
	return icon;
}

QIcon BothPlatformsIcon(int height)
{
	QIcon icon;
	for (qreal dpr : {1.0, 1.5, 2.0}) {
		const int h = qRound(height * dpr);
		const int gap = qRound(2 * dpr);
		QImage image(h * 2 + gap, h, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
		QPainter p(&image);
		p.drawImage(0, 0, PlatformImage(Platform::Twitch, h, 1.0));
		p.drawImage(h + gap, 0, PlatformImage(Platform::YouTube, h, 1.0));
		p.end();
		image.setDevicePixelRatio(dpr);
		icon.addPixmap(QPixmap::fromImage(image));
	}
	return icon;
}

} // namespace unified_chat
