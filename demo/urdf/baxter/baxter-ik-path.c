/* -*- mode: C; c-basic-offset: 4; -*- */
/* ex: set shiftwidth=4 tabstop=4 expandtab: */
/*
 * Copyright (c) 2015, Rice University
 * All rights reserved.
 *
 * Author(s): Neil T. Dantam <ntd@rice.edu>
 *
 *   Redistribution and use in source and binary forms, with or
 *   without modification, are permitted provided that the following
 *   conditions are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of copyright holder the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
 *   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
 *   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 *   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 *   CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 *   USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 *   AND ON ANY HEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *   LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *   ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *   POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "baxter-demo.h"
#include "amino/rx/rxerr.h"
#include "amino/rx/scene_kin.h"
#include "amino/rx/scene_sub.h"
#include "amino/rx/scene_gl.h"
#include "amino/ct/traj.h"
#include "amino/rx/rx_ct.h"

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    // Initialize scene graph
    struct aa_rx_sg *scenegraph = baxter_demo_load_baxter(NULL);
    aa_rx_sg_init(scenegraph);



    /* --- Solve IK --- */
    size_t n_q = aa_rx_sg_config_count(scenegraph);
    aa_rx_frame_id tip_id = aa_rx_sg_frame_id(scenegraph, "right_w2");
    struct aa_rx_sg_sub *ssg = aa_rx_sg_chain_create( scenegraph, AA_RX_FRAME_ROOT, tip_id);
    size_t n_qs = aa_rx_sg_sub_config_count(ssg);
    size_t n_f = aa_rx_sg_frame_count(scenegraph);

    assert( 7 == n_qs );

    // seed position
    double qstart_all[n_q];
    {
        double qstart_s[7] = {
            .05 * M_PI, // s0
            -.25 * M_PI, // s1
            0, // e0
            .25*M_PI, // e1
            0, // w0
            .25*M_PI, // w1
            0 // w2
        };
        AA_MEM_ZERO(qstart_all, n_q);
        aa_rx_sg_config_set( scenegraph, n_q, n_qs, aa_rx_sg_sub_configs(ssg),
                             qstart_s, qstart_all );
    }

    // solver options
    struct aa_rx_ksol_opts *ko = aa_rx_ksol_opts_create();
    aa_rx_ksol_opts_center_configs( ko, ssg, .1 );
    aa_rx_ksol_opts_set_tol_dq( ko, .001 );
    aa_rx_ksol_opts_take_seed( ko, n_q, qstart_all, AA_MEM_BORROW );
    aa_rx_ksol_opts_set_frame( ko, tip_id );
    fprintf(stderr, "tip_id: %d\n", tip_id);

    const struct aa_rx_ik_jac_cx *ik_cx = aa_rx_ik_jac_cx_create(ssg,ko);

    // solver goal
    double qs[n_qs];
    //double E_ref[7];

    double E_ref0[7] = {0, 1, 0, 0,
                       .8, -.25, .3051};
    double E_ref1[7];
    {
        double E0[7] = {0, 1, 0, 0,
                        0.6, -0.0, 0.3};
        double E1[7] = {0, 0, 0, 1,
                        0, 0, 0 };
        aa_tf_xangle2quat(-.5*M_PI, E1 );
        aa_tf_qutr_mul( E0, E1, E_ref1 );
    }
    aa_tick("Inverse Kinematics: ");
    int r = aa_rx_ik_jac_solve( ik_cx,
                                1, E_ref0, 7,
                                n_qs, qs );
    aa_tock();
    if( r) {
        fprintf(stderr, "WARNING: Initial position IK failed\n");
    } else {
        fprintf(stderr, "IK OK\n");
    }
    fprintf(stdout, "q_start_sub: "); aa_dump_vec(stdout, qs, n_qs);

    printf("Eref0: "); aa_dump_vec(stdout, E_ref0, 7);
    printf("Eref1: "); aa_dump_vec(stdout, E_ref1, 7);

    if( r ) {
        char *e = aa_rx_errstr( aa_mem_region_local_get(), r );
        fprintf(stderr, "Oops, IK failed: `%s' (0x%x)\n", e, r);
        aa_mem_region_local_pop(e);
    }

    aa_rx_sg_config_set( scenegraph, n_q, n_qs, aa_rx_sg_sub_configs(ssg),
                         qs, qstart_all );



    // compute trajectory and path
    double *path;
    size_t n_points;
    struct aa_mem_region *reg = aa_mem_region_create(1024);
    struct aa_ct_pt_list *pt_list = aa_ct_pt_list_create(reg);
    aa_ct_pt_list_add_qutr(pt_list, E_ref0);
    aa_ct_pt_list_add_qutr(pt_list, E_ref1);
    struct aa_ct_seg_list *seg_list = aa_ct_tjX_slerp_generate(reg, pt_list);
    aa_rx_ksol_opts_set_dt( ko, .1 );
    aa_rx_ksol_opts_set_gain_trans( ko, .5 );
    aa_rx_ksol_opts_set_gain_angle( ko, .5 );
    aa_rx_ksol_opts_set_tol_dq( ko, .5 );
    /* aa_rx_ksol_opts_set_gain_trans( ko, 0 ); */
    /* aa_rx_ksol_opts_set_gain_angle( ko, 0 ); */
    aa_rx_ct_tjx_path( reg, ko, ssg,
                       seg_list, n_q, qstart_all,
                       &n_points, &path );


    double *path_all = AA_MEM_REGION_NEW_N(reg, double, n_points*n_q);
    //size_t n_points_all = 0;
    for( size_t i = 0; i < n_points; i++  ) {
        double *p_path_sub = path + i*n_qs;
        double *p_path_all = path_all + i*n_q;
        AA_MEM_CPY( p_path_all, qstart_all, n_q );
        aa_rx_sg_sub_config_set( ssg,
                                 n_qs, p_path_sub,
                                 n_q, p_path_all );


        /* double TF_abs[7*n_f], TF_rel[7*n_f]; */
        /* aa_rx_sg_tf( scenegraph, */
        /*              n_q, p_path_all, */
        /*              n_f, */
        /*              TF_rel, 7, */
        /*              TF_abs, 7 ); */
        //printf("IK/FK: "); aa_dump_vec(stdout, TF_abs + 7*tip_id, 7);
    }



    // set all-config joint position

    /*--- setup window ---*/
    struct aa_rx_win * win = baxter_demo_setup_window(scenegraph);
    struct aa_gl_globals *globals = aa_rx_win_gl_globals(win);
    aa_gl_globals_set_show_visual(globals, 1);
    aa_gl_globals_set_show_collision(globals, 0);

    aa_rx_win_set_display_plan(win, scenegraph, n_points, path_all );

    //AA_MEM_CPY(path_all+n_q, path_all + (n_points-1)*n_q, n_q);
    //aa_rx_win_set_display_plan(win, scenegraph, 2, path_all );
    //aa_rx_win_set_config( win, n_q, qstart_all );

    /*--- Do Display ---*/
    aa_rx_win_run();

    /*--- Cleanup ---*/
    aa_rx_sg_destroy(scenegraph);
    aa_rx_win_destroy(win);
    SDL_Quit();

    return 0;
}