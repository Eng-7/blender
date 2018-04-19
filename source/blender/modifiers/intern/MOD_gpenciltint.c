/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software  Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2017, Blender Foundation
 * This is a new part of Blender
 *
 * Contributor(s): Antonio Vazquez
 *
 * ***** END GPL LICENSE BLOCK *****
 *
 */

/** \file blender/modifiers/intern/MOD_gpenciltint.c
 *  \ingroup modifiers
 */

#include <stdio.h>

#include "DNA_scene_types.h"
#include "DNA_object_types.h"
#include "DNA_gpencil_types.h"

#include "BLI_blenlib.h"
#include "BLI_ghash.h"
#include "BLI_math_vector.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"
#include "BKE_global.h"
#include "BKE_gpencil.h"
#include "BKE_paint.h"
#include "BKE_main.h"

#include "DEG_depsgraph.h"

#include "MOD_modifiertypes.h"
#include "MOD_gpencil_util.h"

static void initData(ModifierData *md)
{
	GpencilTintModifierData *gpmd = (GpencilTintModifierData *)md;
	gpmd->pass_index = 0;
	gpmd->factor = 0;
	gpmd->layername[0] = '\0';
	gpmd->flag |= GP_TINT_CREATE_COLORS;
}

static void copyData(ModifierData *md, ModifierData *target)
{
	modifier_copyData_generic(md, target);
}

/* tint strokes */
static void deformStroke(ModifierData *md, Depsgraph *UNUSED(depsgraph),
                         Object *UNUSED(ob), bGPDlayer *gpl, bGPDstroke *gps)
{
#if 0

	GpencilTintModifierData *mmd = (GpencilTintModifierData *)md;

	if (!is_stroke_affected_by_modifier(
	        mmd->layername, mmd->pass_index, 1, gpl, gps,
	        mmd->flag & GP_TINT_INVERSE_LAYER, mmd->flag & GP_TINT_INVERSE_PASS))
	{
		return;
	}

	interp_v3_v3v3(gps->palcolor->rgb, gps->palcolor->rgb, mmd->rgb, mmd->factor);
	interp_v3_v3v3(gps->palcolor->fill, gps->palcolor->fill, mmd->rgb, mmd->factor);

	/* if factor is > 1, the alpha must be changed to get full tint */
	if (mmd->factor > 1.0f) {
		gps->palcolor->rgb[3] += mmd->factor - 1.0f;
		if (gps->palcolor->fill[3] > 1e-5) {
			gps->palcolor->fill[3] += mmd->factor - 1.0f;
		}
	}

	CLAMP4(gps->palcolor->rgb, 0.0f, 1.0f);
	CLAMP4(gps->palcolor->fill, 0.0f, 1.0f);
	
	/* if factor > 1.0, affect the strength of the stroke */
	if (mmd->factor > 1.0f) {
		for (int i = 0; i < gps->totpoints; i++) {
			bGPDspoint *pt = &gps->points[i];
			pt->strength += mmd->factor - 1.0f;
			CLAMP(pt->strength, 0.0f, 1.0f);
		}
	}
#endif
}

static void bakeModifierGP(const bContext *C, Depsgraph *depsgraph,
                           ModifierData *md, Object *ob)
{
#if 0

	GpencilTintModifierData *mmd = (GpencilTintModifierData *)md;
	Main *bmain = CTX_data_main(C);
	bGPdata *gpd = ob->data;
	Palette *newpalette = NULL;
	
	GHash *gh_layer = BLI_ghash_str_new("GP_Tint Layer modifier");
	GHash *gh_color;

	for (bGPDlayer *gpl = gpd->layers.first; gpl; gpl = gpl->next) {
		for (bGPDframe *gpf = gpl->frames.first; gpf; gpf = gpf->next) {
			for (bGPDstroke *gps = gpf->strokes.first; gps; gps = gps->next) {
				/* skip stroke if it doesn't have color info */
				if (ELEM(NULL, gps->palette, gps->palcolor))
					continue;
				
				/* look for palette */
				gh_color = (GHash *)BLI_ghash_lookup(gh_layer, gps->palette->id.name);
				if (gh_color == NULL) {
					gh_color = BLI_ghash_str_new("GP_Tint Color modifier");
					BLI_ghash_insert(gh_layer, gps->palette->id.name, gh_color);
				}

				/* look for color */
				PaletteColor *gps_palcolor = BKE_palette_color_getbyname(gps->palette, gps->colorname);
				PaletteColor *newpalcolor = (PaletteColor *)BLI_ghash_lookup(gh_color, gps->colorname);
				if (newpalcolor == NULL) {
					if (mmd->flag & GP_TINT_CREATE_COLORS) {
						if (!newpalette) {
							bGPDpaletteref *palslot = BKE_gpencil_paletteslot_addnew(bmain, gpd, "Tinted Colors");
							newpalette = palslot->palette;
						}
						newpalcolor = BKE_palette_color_copy(newpalette, gps_palcolor);
						BLI_strncpy(gps->colorname, newpalcolor->info, sizeof(gps->colorname));
					}
					BLI_ghash_insert(gh_color, gps_palcolor->info, newpalcolor);

					deformStroke(md, depsgraph, ob, gpl, gps);
				}
			}
		}
	}
	/* free hash buffers */
	GHashIterator *ihash = BLI_ghashIterator_new(gh_layer);
	while (!BLI_ghashIterator_done(ihash)) {
		GHash *gh = BLI_ghashIterator_getValue(ihash);
		if (gh) {
			BLI_ghash_free(gh, NULL, NULL);
			gh = NULL;
		}
		BLI_ghashIterator_step(ihash);
	}
	BLI_ghashIterator_free(ihash);

	if (gh_layer) {
		BLI_ghash_free(gh_layer, NULL, NULL);
		gh_layer = NULL;
	}
#endif
}

ModifierTypeInfo modifierType_GpencilTint = {
	/* name */              "Tint",
	/* structName */        "GpencilTintModifierData",
	/* structSize */        sizeof(GpencilTintModifierData),
	/* type */              eModifierTypeType_Gpencil,
	/* flags */             eModifierTypeFlag_GpencilMod | eModifierTypeFlag_SupportsEditmode,

	/* copyData */          copyData,
	/* deformVerts */       NULL,
	/* deformMatrices */    NULL,
	/* deformVertsEM */     NULL,
	/* deformMatricesEM */  NULL,
	/* applyModifier */     NULL,
	/* applyModifierEM */   NULL,
	/* deformStroke */      deformStroke,
	/* generateStrokes */   NULL,
	/* bakeModifierGP */    bakeModifierGP,
	/* initData */          initData,
	/* requiredDataMask */  NULL,
	/* freeData */          NULL,
	/* isDisabled */        NULL,
	/* updateDepsgraph */   NULL,
	/* dependsOnTime */     NULL,
	/* dependsOnNormals */	NULL,
	/* foreachObjectLink */ NULL,
	/* foreachIDLink */     NULL,
	/* foreachTexLink */    NULL,
};
