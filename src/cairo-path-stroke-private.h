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

#ifndef CAIRO_PATH_STROKE_PRIVATE_H
#define CAIRO_PATH_STROKE_PRIVATE_H

#include "cairoint.h"

static inline cairo_bool_t
_compute_normalized_device_slope (double *dx, double *dy,
				  const cairo_matrix_t *ctm_inverse,
				  double *mag_out)
{
    double dx0 = *dx, dy0 = *dy;
    double mag;

    cairo_matrix_transform_distance (ctm_inverse, &dx0, &dy0);

    if (dx0 == 0.0 && dy0 == 0.0) {
	if (mag_out)
	    *mag_out = 0.0;
	return FALSE;
    }

    if (dx0 == 0.0) {
	*dx = 0.0;
	if (dy0 > 0.0) {
	    mag = dy0;
	    *dy = 1.0;
	} else {
	    mag = -dy0;
	    *dy = -1.0;
	}
    } else if (dy0 == 0.0) {
	*dy = 0.0;
	if (dx0 > 0.0) {
	    mag = dx0;
	    *dx = 1.0;
	} else {
	    mag = -dx0;
	    *dx = -1.0;
	}
    } else {
	mag = hypot (dx0, dy0);
	*dx = dx0 / mag;
	*dy = dy0 / mag;
    }

    if (mag_out)
	*mag_out = mag;

    return TRUE;
}

cairo_private cairo_status_t
_cairo_path_fixed_stroke_to_shaper (cairo_path_fixed_t	*path,
				   const cairo_stroke_style_t	*stroke_style,
				   const cairo_matrix_t	*ctm,
				   const cairo_matrix_t	*ctm_inverse,
				   double		 tolerance,
				   cairo_status_t (*add_triangle) (void *closure,
								   const cairo_point_t triangle[3]),
				   cairo_status_t (*add_triangle_fan) (void *closure,
								       const cairo_point_t *midpt,
								       const cairo_point_t *points,
								       int npoints),
				   cairo_status_t (*add_quad) (void *closure,
							       const cairo_point_t quad[4]),
				   void *closure);

cairo_private cairo_bool_t
_cairo_path_fixed_stroke_can_use_hairline_shaper (const cairo_stroke_style_t *style,
						  const cairo_matrix_t *ctm);

cairo_private cairo_status_t
_cairo_path_fixed_stroke_to_hairline_shaper (const cairo_path_fixed_t *path,
					     const cairo_stroke_style_t *style,
					     const cairo_matrix_t *ctm,
					     const cairo_matrix_t *ctm_inverse,
					     double tolerance,
					     cairo_status_t (*add_segment) (void *closure,
									    const cairo_point_t *p1,
									    const cairo_point_t *p2),
					     void *closure);



#endif /* CAIRO_PATH_STROKE_PRIVATE_H */
