/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2012 Samsung Electronics
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is Red Hat, Inc.
 *
 * Contributor(s):
 *	Henry Song <hsong@sisa.samsung.com>
 */

#include "cairo-gl-private.h"
#include "cairo-slope-private.h"
#include "cairo-path-stroke-private.h"

#define SCALE_TOLERANCE 0.0000001

typedef struct cairo_hairline_stroker {
    double tolerance;
    cairo_stroker_dash_t dash;
    cairo_matrix_t *ctm;
    cairo_matrix_t *ctm_inverse;
    cairo_line_cap_t cap_style;

    cairo_point_t current_point;
    cairo_point_t first_subpath_point;
    cairo_point_t last_subpath_segment_endpoint;
    double last_subpath_segment_dx;
    double last_subpath_segment_dy;
    cairo_bool_t drew_segments_on_subpath;
    cairo_bool_t closing_subpath;

    double cap_and_join_dx;
    double cap_and_join_dy;

    void *closure;

    cairo_status_t (*add_segment) (void *closure,
				   const cairo_point_t *p1,
				   const cairo_point_t *p2);
} cairo_hairline_stroker_t;

cairo_bool_t
_cairo_path_fixed_stroke_can_use_hairline_shaper (const cairo_stroke_style_t *style,
						  const cairo_matrix_t *ctm)
{
    double x, y;

    if (style->line_width != 1.0)
	return FALSE;

    if (style->line_join == CAIRO_LINE_JOIN_MITER && style->miter_limit > 10.0)
	return FALSE;

    if ( _cairo_matrix_compute_basis_scale_factors (ctm, &x, &y, TRUE))
        return FALSE;
    if (fabs (x - 1.0) > SCALE_TOLERANCE || fabs (y - 1.0) > SCALE_TOLERANCE)
	return FALSE;

    return TRUE;
}

static cairo_status_t
add_trailing_cap_to_last_segment (cairo_hairline_stroker_t *stroker)
{
    cairo_point_t p1, p2;
    p1 = p2 = stroker->last_subpath_segment_endpoint;

    p2.x += _cairo_fixed_from_double (stroker->cap_and_join_dx *
				      stroker->last_subpath_segment_dx);
    p2.y += _cairo_fixed_from_double (stroker->cap_and_join_dy *
				      stroker->last_subpath_segment_dy);
    return stroker->add_segment (stroker->closure, &p1, &p2);
}

static cairo_status_t
add_stroker_segment (cairo_hairline_stroker_t *stroker,
		     const cairo_point_t *p1,
		     const cairo_point_t *p2,
		     double dx, double dy)
{
    cairo_point_t segment[2] = { *p1, *p2 };
    cairo_bool_t using_butt_cap = stroker->cap_style == CAIRO_LINE_CAP_BUTT;

    /* All this logic here is based on the idea that for a hairline, all kinds
     * of joins and all caps except butt caps can be drawn by moving in the
     * slope direction half the line width (0.5 device units). */
    if (stroker->drew_segments_on_subpath) {
	cairo_bool_t last_segment_touches =
	    stroker->last_subpath_segment_endpoint.x == p1->x &&
	    stroker->last_subpath_segment_endpoint.y == p1->y;

	/* We've already drawn segments for this subpath, but they don't touch
	 * the segment we are about to draw, so we need to add a cap to that
	 * previous segment. */
	if (! last_segment_touches && ! using_butt_cap) {
	    cairo_status_t status = add_trailing_cap_to_last_segment (stroker);
	    if (unlikely (status))
		return status;
	}

	/* We've already drawn segments for this subpath. If we are touching
	 * the last one, we draw a join. If we aren't touching the last one,
	 * we draw the leading cap. */
	if (last_segment_touches || ! using_butt_cap) {
	    segment[0].x +=
		_cairo_fixed_from_double (-stroker->cap_and_join_dx * dx);
	    segment[0].y +=
		_cairo_fixed_from_double (-stroker->cap_and_join_dx * dy);
	}
    } else if (! using_butt_cap) {
	/* This is the first segment on this subpath. We don't yet know if
	 * the final segment will connect with us, but a leading cap and a
	 * join look he same (apart from butt caps), so we can draw that now. */
	segment[0].x += _cairo_fixed_from_double (-stroker->cap_and_join_dx * dx);
	segment[0].y += _cairo_fixed_from_double (-stroker->cap_and_join_dy * dy);
    }

    /* It's time to close the subpath and we are actually using butt caps, so
     * we need to draw a join as well, since we didn't do it when rendering the
     * first segment. */
    if (stroker->closing_subpath &&
	stroker->cap_style == CAIRO_LINE_CAP_BUTT &&
	p2->x == stroker->first_subpath_point.x &&
	p2->y == stroker->first_subpath_point.y) {
	segment[1].x += _cairo_fixed_from_double (stroker->cap_and_join_dx * dx);
	segment[1].y += _cairo_fixed_from_double (stroker->cap_and_join_dy * dy);
    }

    stroker->drew_segments_on_subpath = TRUE;
    stroker->last_subpath_segment_endpoint = *p2;
    stroker->last_subpath_segment_dx = dx;
    stroker->last_subpath_segment_dy = dy;

    return stroker->add_segment (stroker->closure, &segment[0], &segment[1]);
}

static cairo_status_t
move_to (void *closure,
	 const cairo_point_t *point)
{
    cairo_hairline_stroker_t *stroker = closure;
    cairo_status_t status = CAIRO_STATUS_SUCCESS;

    _cairo_stroker_dash_start (&stroker->dash);

    if (stroker->drew_segments_on_subpath &&
	stroker->cap_style != CAIRO_LINE_CAP_BUTT) {
	status = add_trailing_cap_to_last_segment (stroker);
	stroker->drew_segments_on_subpath = FALSE;
    }

    stroker->first_subpath_point = *point;
    stroker->current_point = *point;

    return status;
}

static cairo_status_t
line_to (void *closure,
	 const cairo_point_t *p2)
{
    cairo_hairline_stroker_t *stroker = closure;
    const cairo_point_t *p1 = &stroker->current_point;
    cairo_status_t status;

    cairo_slope_t dev_slope;
    double slope_dx, slope_dy;
    double mag;

    _cairo_slope_init (&dev_slope, p1, p2);
    slope_dx = _cairo_fixed_to_double (p2->x - p1->x);
    slope_dy = _cairo_fixed_to_double (p2->y - p1->y);
    if (! _compute_normalized_device_slope (&slope_dx, &slope_dy,
					    stroker->ctm_inverse, &mag))
        return CAIRO_STATUS_SUCCESS;

    status = add_stroker_segment (stroker, p1, p2, slope_dx, slope_dy);
    stroker->current_point = *p2;
    return status;
}

static cairo_status_t
line_to_dashed (void *closure,
		const cairo_point_t *p2)
{
    cairo_hairline_stroker_t *stroker = closure;
    const cairo_point_t *p1 = &stroker->current_point;
    cairo_point_t dash_p1, dash_p2;
    cairo_status_t status;

    double slope_dx, slope_dy;
    double remain, mag;
    cairo_slope_t dev_slope;

    _cairo_slope_init (&dev_slope, p1, p2);
    slope_dx = _cairo_fixed_to_double (p2->x - p1->x);
    slope_dy = _cairo_fixed_to_double (p2->y - p1->y);
    if (! _compute_normalized_device_slope (&slope_dx, &slope_dy,
					    stroker->ctm_inverse, &mag))
	return CAIRO_STATUS_SUCCESS;

    remain = mag;
    dash_p1 = *p1;
    while (remain) {
	double step_length, dash_dx, dash_dy;

	step_length = MIN (stroker->dash.dash_remain, remain);
	remain -= step_length;

	dash_dx = slope_dx * (mag - remain);
	dash_dy = slope_dy * (mag - remain);
	cairo_matrix_transform_distance (stroker->ctm, &dash_dx, &dash_dy);

	dash_p2.x = _cairo_fixed_from_double (dash_dx) + p1->x;
	dash_p2.y = _cairo_fixed_from_double (dash_dy) + p1->y;
	if (stroker->dash.dash_on) {
	    status = add_stroker_segment (stroker, &dash_p1, &dash_p2,
					  slope_dx, slope_dy);
	    if (unlikely (status))
		return status;
	}

	_cairo_stroker_dash_step (&stroker->dash, step_length);
	dash_p1 = dash_p2;
    }

    stroker->current_point = *p2;
    return CAIRO_STATUS_SUCCESS;
}

static cairo_status_t
curve_to (void *closure,
	  const cairo_point_t *p0,
	  const cairo_point_t *p1,
	  const cairo_point_t *p2)
{
    cairo_hairline_stroker_t *stroker = closure;
    cairo_spline_t spline;
    cairo_path_fixed_line_to_func_t *line_to_func;

    line_to_func = stroker->dash.dashed ?  line_to_dashed : line_to;
    if (! _cairo_spline_init (&spline, (cairo_spline_add_point_func_t)line_to,
			      closure, &stroker->current_point, p0, p1, p2))
	return line_to_func (closure, p2);

    return _cairo_spline_decompose (&spline, stroker->tolerance);
}

static cairo_status_t
close_path (void *closure)
{
    cairo_hairline_stroker_t *stroker = closure;
    cairo_status_t status;

    stroker->closing_subpath = TRUE;
    if (stroker->dash.dashed)
	status = line_to_dashed (closure, &stroker->first_subpath_point);
    else
	status = line_to (closure, &stroker->first_subpath_point);
    stroker->closing_subpath = FALSE;

    return status;
}

cairo_status_t
_cairo_path_fixed_stroke_to_hairline_shaper (const cairo_path_fixed_t *path,
					     const cairo_stroke_style_t *style,
					     const cairo_matrix_t *ctm,
					     const cairo_matrix_t *ctm_inverse,
					     double tolerance,
					     cairo_status_t (*add_segment) (void *closure,
									    const cairo_point_t *p1,
									    const cairo_point_t *p2),
					     void *closure)
{
    cairo_path_fixed_line_to_func_t *line_to_func;
    cairo_hairline_stroker_t stroker;

    stroker.ctm = (cairo_matrix_t *)ctm;
    stroker.ctm_inverse = (cairo_matrix_t *)ctm_inverse;
    stroker.tolerance = tolerance;
    stroker.cap_style = style->line_cap;
    _cairo_stroker_dash_init (&stroker.dash, style);

    stroker.closure = closure;
    stroker.add_segment = add_segment;
    stroker.closing_subpath = FALSE;

    stroker.cap_and_join_dx = stroker.cap_and_join_dy = 0.5;
    cairo_matrix_transform_distance (stroker.ctm,
				     &stroker.cap_and_join_dx,
				     &stroker.cap_and_join_dy);

    line_to_func = style->dash ? line_to_dashed : line_to;
    return _cairo_path_fixed_interpret (path,
					move_to,
					line_to_func,
					curve_to,
					close_path,
					&stroker);
}
