/*
 * Copyright 2014, General Dynamics C4 Systems
 *
 * This software may be distributed and modified according to the terms of
 * the GNU General Public License version 2. Note that NO WARRANTY is provided.
 * See "LICENSE_GPLv2.txt" for details.
 *
 * @TAG(GD_GPL)
 */

#include <config.h>
#include <api/syscall.h>
#include <machine/io.h>
#include <kernel/boot.h>
#include <model/statedata.h>
#include <arch/kernel/vspace.h>
#include <arch/api/invocation.h>

struct findVSpaceForASID_ret {
    exception_t status;
    void *vspace_root;
};
typedef struct findVSpaceForASID_ret findVSpaceForASID_ret_t;

struct lookupPTSlot_ret {
    exception_t status;
    pte_t*      ptSlot;
};
typedef struct lookupPTSlot_ret lookupPTSlot_ret_t;

/* 'gdt_idt_ptr' is declared globally because of a C-subset restriction.
 * It is only used in init_drts(), which therefore is non-reentrant.
 */
gdt_idt_ptr_t gdt_idt_ptr;

/* initialise the Task State Segment (TSS) */

BOOT_CODE static void
init_tss(tss_t* tss)
{
    tss_ptr_new(
        tss,
        MASK(16),       /* io_map_base  */
        0,              /* trap         */
        SEL_NULL,       /* sel_ldt      */
        SEL_NULL,       /* gs           */
        SEL_NULL,       /* fs           */
        SEL_NULL,       /* ds           */
        SEL_NULL,       /* ss           */
        SEL_NULL,       /* cs           */
        SEL_NULL,       /* es           */
        0,              /* edi          */
        0,              /* esi          */
        0,              /* ebp          */
        0,              /* esp          */
        0,              /* ebx          */
        0,              /* edx          */
        0,              /* ecx          */
        0,              /* eax          */
        0,              /* eflags       */
        0,              /* eip          */
        0,              /* cr3          */
        SEL_NULL,       /* ss2          */
        0,              /* esp2         */
        SEL_NULL,       /* ss1          */
        0,              /* esp1         */
        SEL_DS_0,       /* ss0          */
        0,              /* esp0         */
        0               /* prev_task    */
    );
}
/* initialise Global Descriptor Table (GDT) */

BOOT_CODE static void
init_gdt(gdt_entry_t* gdt, tss_t* tss)
{
    uint32_t tss_addr = (uint32_t)tss;

    /* Set the NULL descriptor */
    gdt[GDT_NULL] = gdt_entry_gdt_null_new();

    /* 4GB flat kernel code segment on ring 0 descriptor */
    gdt[GDT_CS_0] = gdt_entry_gdt_code_new(
                        0,      /* Base high 8 bits             */
                        1,      /* Granularity                  */
                        1,      /* Operation size               */
                        0,      /* Available                    */
                        0xf,    /* Segment limit high 4 bits    */
                        1,      /* Present                      */
                        0,      /* Descriptor privilege level   */
                        1,      /* readable                     */
                        1,      /* accessed                     */
                        0,      /* Base middle 8 bits           */
                        0,      /* Base low 16 bits             */
                        0xffff  /* Segment limit low 16 bits    */
                    );

    /* 4GB flat kernel data segment on ring 0 descriptor */
    gdt[GDT_DS_0] = gdt_entry_gdt_data_new(
                        0,      /* Base high 8 bits             */
                        1,      /* Granularity                  */
                        1,      /* Operation size               */
                        0,      /* Available                    */
                        0xf,    /* Segment limit high 4 bits    */
                        1,      /* Present                      */
                        0,      /* Descriptor privilege level   */
                        1,      /* writable                     */
                        1,      /* accessed                     */
                        0,      /* Base middle 8 bits           */
                        0,      /* Base low 16 bits             */
                        0xffff  /* Segment limit low 16 bits    */
                    );

    /* 4GB flat userland code segment on ring 3 descriptor */
    gdt[GDT_CS_3] = gdt_entry_gdt_code_new(
                        0,      /* Base high 8 bits             */
                        1,      /* Granularity                  */
                        1,      /* Operation size               */
                        0,      /* Available                    */
                        0xf,    /* Segment limit high 4 bits    */
                        1,      /* Present                      */
                        3,      /* Descriptor privilege level   */
                        1,      /* readable                     */
                        1,      /* accessed                     */
                        0,      /* Base middle 8 bits           */
                        0,      /* Base low 16 bits             */
                        0xffff  /* Segment limit low 16 bits    */
                    );

    /* 4GB flat userland data segment on ring 3 descriptor */
    gdt[GDT_DS_3] = gdt_entry_gdt_data_new(
                        0,      /* Base high 8 bits             */
                        1,      /* Granularity                  */
                        1,      /* Operation size               */
                        0,      /* Available                    */
                        0xf,    /* Segment limit high 4 bits    */
                        1,      /* Present                      */
                        3,      /* Descriptor privilege level   */
                        1,      /* writable                     */
                        1,      /* accessed                     */
                        0,      /* Base middle 8 bits           */
                        0,      /* Base low 16 bits             */
                        0xffff  /* Segment limit low 16 bits    */
                    );

    /* Task State Segment (TSS) descriptor */
    gdt[GDT_TSS] = gdt_entry_gdt_tss_new(
                       tss_addr >> 24,            /* base_high 8 bits     */
                       0,                           /* granularity          */
                       0,                           /* avl                  */
                       0,                           /* limit_high 4 bits    */
                       1,                           /* present              */
                       0,                           /* dpl                  */
                       0,                           /* busy                 */
                       1,                           /* always_true          */
                       (tss_addr >> 16) & 0xff,     /* base_mid 8 bits      */
                       (tss_addr & 0xffff),         /* base_low 16 bits     */
                       sizeof(tss_t) - 1            /* limit_low 16 bits    */
                   );

    /* pre-init the userland data segment used for TLS */
    gdt[GDT_TLS] = gdt_entry_gdt_data_new(
                       0,      /* Base high 8 bits             */
                       1,      /* Granularity                  */
                       1,      /* Operation size               */
                       0,      /* Available                    */
                       0xf,    /* Segment limit high 4 bits    */
                       1,      /* Present                      */
                       3,      /* Descriptor privilege level   */
                       1,      /* writable                     */
                       1,      /* accessed                     */
                       0,      /* Base middle 8 bits           */
                       0,      /* Base low 16 bits             */
                       0xffff  /* Segment limit low 16 bits    */
                   );

    /* pre-init the userland data segment used for the IPC buffer */
    gdt[GDT_IPCBUF] = gdt_entry_gdt_data_new(
                          0,      /* Base high 8 bits             */
                          1,      /* Granularity                  */
                          1,      /* Operation size               */
                          0,      /* Available                    */
                          0xf,    /* Segment limit high 4 bits    */
                          1,      /* Present                      */
                          3,      /* Descriptor privilege level   */
                          1,      /* writable                     */
                          1,      /* accessed                     */
                          0,      /* Base middle 8 bits           */
                          0,      /* Base low 16 bits             */
                          0xffff  /* Segment limit low 16 bits    */
                      );
}

/* initialise the Interrupt Descriptor Table (IDT) */

BOOT_CODE static void
init_idt_entry(idt_entry_t* idt, interrupt_t interrupt, void(*handler)(void))
{
    uint32_t handler_addr = (uint32_t)handler;
    uint32_t dpl = 3;

    if (interrupt < int_trap_min) {
        dpl = 0;
    }

    idt[interrupt] = idt_entry_interrupt_gate_new(
                         handler_addr >> 16,   /* offset_high  */
                         1,                    /* present      */
                         dpl,                  /* dpl          */
                         1,                    /* gate_size    */
                         SEL_CS_0,             /* seg_selector */
                         handler_addr & 0xffff /* offset_low   */
                     );
}

BOOT_CODE static void
init_idt(idt_entry_t* idt)
{
    init_idt_entry(idt, 0x00, int_00);
    init_idt_entry(idt, 0x01, int_01);
    init_idt_entry(idt, 0x02, int_02);
    init_idt_entry(idt, 0x03, int_03);
    init_idt_entry(idt, 0x04, int_04);
    init_idt_entry(idt, 0x05, int_05);
    init_idt_entry(idt, 0x06, int_06);
    init_idt_entry(idt, 0x07, int_07);
    init_idt_entry(idt, 0x08, int_08);
    init_idt_entry(idt, 0x09, int_09);
    init_idt_entry(idt, 0x0a, int_0a);
    init_idt_entry(idt, 0x0b, int_0b);
    init_idt_entry(idt, 0x0c, int_0c);
    init_idt_entry(idt, 0x0d, int_0d);
    init_idt_entry(idt, 0x0e, int_0e);
    init_idt_entry(idt, 0x0f, int_0f);

    init_idt_entry(idt, 0x10, int_10);
    init_idt_entry(idt, 0x11, int_11);
    init_idt_entry(idt, 0x12, int_12);
    init_idt_entry(idt, 0x13, int_13);
    init_idt_entry(idt, 0x14, int_14);
    init_idt_entry(idt, 0x15, int_15);
    init_idt_entry(idt, 0x16, int_16);
    init_idt_entry(idt, 0x17, int_17);
    init_idt_entry(idt, 0x18, int_18);
    init_idt_entry(idt, 0x19, int_19);
    init_idt_entry(idt, 0x1a, int_1a);
    init_idt_entry(idt, 0x1b, int_1b);
    init_idt_entry(idt, 0x1c, int_1c);
    init_idt_entry(idt, 0x1d, int_1d);
    init_idt_entry(idt, 0x1e, int_1e);
    init_idt_entry(idt, 0x1f, int_1f);

    init_idt_entry(idt, 0x20, int_20);
    init_idt_entry(idt, 0x21, int_21);
    init_idt_entry(idt, 0x22, int_22);
    init_idt_entry(idt, 0x23, int_23);
    init_idt_entry(idt, 0x24, int_24);
    init_idt_entry(idt, 0x25, int_25);
    init_idt_entry(idt, 0x26, int_26);
    init_idt_entry(idt, 0x27, int_27);
    init_idt_entry(idt, 0x28, int_28);
    init_idt_entry(idt, 0x29, int_29);
    init_idt_entry(idt, 0x2a, int_2a);
    init_idt_entry(idt, 0x2b, int_2b);
    init_idt_entry(idt, 0x2c, int_2c);
    init_idt_entry(idt, 0x2d, int_2d);
    init_idt_entry(idt, 0x2e, int_2e);
    init_idt_entry(idt, 0x2f, int_2f);

    init_idt_entry(idt, 0x30, int_30);
    init_idt_entry(idt, 0x31, int_31);
    init_idt_entry(idt, 0x32, int_32);
    init_idt_entry(idt, 0x33, int_33);
    init_idt_entry(idt, 0x34, int_34);
    init_idt_entry(idt, 0x35, int_35);
    init_idt_entry(idt, 0x36, int_36);
    init_idt_entry(idt, 0x37, int_37);
    init_idt_entry(idt, 0x38, int_38);
    init_idt_entry(idt, 0x39, int_39);
    init_idt_entry(idt, 0x3a, int_3a);
    init_idt_entry(idt, 0x3b, int_3b);
    init_idt_entry(idt, 0x3c, int_3c);
    init_idt_entry(idt, 0x3d, int_3d);
    init_idt_entry(idt, 0x3e, int_3e);
    init_idt_entry(idt, 0x3f, int_3f);

    init_idt_entry(idt, 0x40, int_40);
    init_idt_entry(idt, 0x41, int_41);
    init_idt_entry(idt, 0x42, int_42);
    init_idt_entry(idt, 0x43, int_43);
    init_idt_entry(idt, 0x44, int_44);
    init_idt_entry(idt, 0x45, int_45);
    init_idt_entry(idt, 0x46, int_46);
    init_idt_entry(idt, 0x47, int_47);
    init_idt_entry(idt, 0x48, int_48);
    init_idt_entry(idt, 0x49, int_49);
    init_idt_entry(idt, 0x4a, int_4a);
    init_idt_entry(idt, 0x4b, int_4b);
    init_idt_entry(idt, 0x4c, int_4c);
    init_idt_entry(idt, 0x4d, int_4d);
    init_idt_entry(idt, 0x4e, int_4e);
    init_idt_entry(idt, 0x4f, int_4f);

    init_idt_entry(idt, 0x50, int_50);
    init_idt_entry(idt, 0x51, int_51);
    init_idt_entry(idt, 0x52, int_52);
    init_idt_entry(idt, 0x53, int_53);
    init_idt_entry(idt, 0x54, int_54);
    init_idt_entry(idt, 0x55, int_55);
    init_idt_entry(idt, 0x56, int_56);
    init_idt_entry(idt, 0x57, int_57);
    init_idt_entry(idt, 0x58, int_58);
    init_idt_entry(idt, 0x59, int_59);
    init_idt_entry(idt, 0x5a, int_5a);
    init_idt_entry(idt, 0x5b, int_5b);
    init_idt_entry(idt, 0x5c, int_5c);
    init_idt_entry(idt, 0x5d, int_5d);
    init_idt_entry(idt, 0x5e, int_5e);
    init_idt_entry(idt, 0x5f, int_5f);

    init_idt_entry(idt, 0x60, int_60);
    init_idt_entry(idt, 0x61, int_61);
    init_idt_entry(idt, 0x62, int_62);
    init_idt_entry(idt, 0x63, int_63);
    init_idt_entry(idt, 0x64, int_64);
    init_idt_entry(idt, 0x65, int_65);
    init_idt_entry(idt, 0x66, int_66);
    init_idt_entry(idt, 0x67, int_67);
    init_idt_entry(idt, 0x68, int_68);
    init_idt_entry(idt, 0x69, int_69);
    init_idt_entry(idt, 0x6a, int_6a);
    init_idt_entry(idt, 0x6b, int_6b);
    init_idt_entry(idt, 0x6c, int_6c);
    init_idt_entry(idt, 0x6d, int_6d);
    init_idt_entry(idt, 0x6e, int_6e);
    init_idt_entry(idt, 0x6f, int_6f);

    init_idt_entry(idt, 0x70, int_70);
    init_idt_entry(idt, 0x71, int_71);
    init_idt_entry(idt, 0x72, int_72);
    init_idt_entry(idt, 0x73, int_73);
    init_idt_entry(idt, 0x74, int_74);
    init_idt_entry(idt, 0x75, int_75);
    init_idt_entry(idt, 0x76, int_76);
    init_idt_entry(idt, 0x77, int_77);
    init_idt_entry(idt, 0x78, int_78);
    init_idt_entry(idt, 0x79, int_79);
    init_idt_entry(idt, 0x7a, int_7a);
    init_idt_entry(idt, 0x7b, int_7b);
    init_idt_entry(idt, 0x7c, int_7c);
    init_idt_entry(idt, 0x7d, int_7d);
    init_idt_entry(idt, 0x7e, int_7e);
    init_idt_entry(idt, 0x7f, int_7f);

    init_idt_entry(idt, 0x80, int_80);
    init_idt_entry(idt, 0x81, int_81);
    init_idt_entry(idt, 0x82, int_82);
    init_idt_entry(idt, 0x83, int_83);
    init_idt_entry(idt, 0x84, int_84);
    init_idt_entry(idt, 0x85, int_85);
    init_idt_entry(idt, 0x86, int_86);
    init_idt_entry(idt, 0x87, int_87);
    init_idt_entry(idt, 0x88, int_88);
    init_idt_entry(idt, 0x89, int_89);
    init_idt_entry(idt, 0x8a, int_8a);
    init_idt_entry(idt, 0x8b, int_8b);
    init_idt_entry(idt, 0x8c, int_8c);
    init_idt_entry(idt, 0x8d, int_8d);
    init_idt_entry(idt, 0x8e, int_8e);
    init_idt_entry(idt, 0x8f, int_8f);

    init_idt_entry(idt, 0x90, int_90);
    init_idt_entry(idt, 0x91, int_91);
    init_idt_entry(idt, 0x92, int_92);
    init_idt_entry(idt, 0x93, int_93);
    init_idt_entry(idt, 0x94, int_94);
    init_idt_entry(idt, 0x95, int_95);
    init_idt_entry(idt, 0x96, int_96);
    init_idt_entry(idt, 0x97, int_97);
    init_idt_entry(idt, 0x98, int_98);
    init_idt_entry(idt, 0x99, int_99);
    init_idt_entry(idt, 0x9a, int_9a);
    init_idt_entry(idt, 0x9b, int_9b);
    init_idt_entry(idt, 0x9c, int_9c);
    init_idt_entry(idt, 0x9d, int_9d);
    init_idt_entry(idt, 0x9e, int_9e);
    init_idt_entry(idt, 0x9f, int_9f);

    init_idt_entry(idt, 0xa0, int_a0);
    init_idt_entry(idt, 0xa1, int_a1);
    init_idt_entry(idt, 0xa2, int_a2);
    init_idt_entry(idt, 0xa3, int_a3);
    init_idt_entry(idt, 0xa4, int_a4);
    init_idt_entry(idt, 0xa5, int_a5);
    init_idt_entry(idt, 0xa6, int_a6);
    init_idt_entry(idt, 0xa7, int_a7);
    init_idt_entry(idt, 0xa8, int_a8);
    init_idt_entry(idt, 0xa9, int_a9);
    init_idt_entry(idt, 0xaa, int_aa);
    init_idt_entry(idt, 0xab, int_ab);
    init_idt_entry(idt, 0xac, int_ac);
    init_idt_entry(idt, 0xad, int_ad);
    init_idt_entry(idt, 0xae, int_ae);
    init_idt_entry(idt, 0xaf, int_af);

    init_idt_entry(idt, 0xb0, int_b0);
    init_idt_entry(idt, 0xb1, int_b1);
    init_idt_entry(idt, 0xb2, int_b2);
    init_idt_entry(idt, 0xb3, int_b3);
    init_idt_entry(idt, 0xb4, int_b4);
    init_idt_entry(idt, 0xb5, int_b5);
    init_idt_entry(idt, 0xb6, int_b6);
    init_idt_entry(idt, 0xb7, int_b7);
    init_idt_entry(idt, 0xb8, int_b8);
    init_idt_entry(idt, 0xb9, int_b9);
    init_idt_entry(idt, 0xba, int_ba);
    init_idt_entry(idt, 0xbb, int_bb);
    init_idt_entry(idt, 0xbc, int_bc);
    init_idt_entry(idt, 0xbd, int_bd);
    init_idt_entry(idt, 0xbe, int_be);
    init_idt_entry(idt, 0xbf, int_bf);

    init_idt_entry(idt, 0xc0, int_c0);
    init_idt_entry(idt, 0xc1, int_c1);
    init_idt_entry(idt, 0xc2, int_c2);
    init_idt_entry(idt, 0xc3, int_c3);
    init_idt_entry(idt, 0xc4, int_c4);
    init_idt_entry(idt, 0xc5, int_c5);
    init_idt_entry(idt, 0xc6, int_c6);
    init_idt_entry(idt, 0xc7, int_c7);
    init_idt_entry(idt, 0xc8, int_c8);
    init_idt_entry(idt, 0xc9, int_c9);
    init_idt_entry(idt, 0xca, int_ca);
    init_idt_entry(idt, 0xcb, int_cb);
    init_idt_entry(idt, 0xcc, int_cc);
    init_idt_entry(idt, 0xcd, int_cd);
    init_idt_entry(idt, 0xce, int_ce);
    init_idt_entry(idt, 0xcf, int_cf);

    init_idt_entry(idt, 0xd0, int_d0);
    init_idt_entry(idt, 0xd1, int_d1);
    init_idt_entry(idt, 0xd2, int_d2);
    init_idt_entry(idt, 0xd3, int_d3);
    init_idt_entry(idt, 0xd4, int_d4);
    init_idt_entry(idt, 0xd5, int_d5);
    init_idt_entry(idt, 0xd6, int_d6);
    init_idt_entry(idt, 0xd7, int_d7);
    init_idt_entry(idt, 0xd8, int_d8);
    init_idt_entry(idt, 0xd9, int_d9);
    init_idt_entry(idt, 0xda, int_da);
    init_idt_entry(idt, 0xdb, int_db);
    init_idt_entry(idt, 0xdc, int_dc);
    init_idt_entry(idt, 0xdd, int_dd);
    init_idt_entry(idt, 0xde, int_de);
    init_idt_entry(idt, 0xdf, int_df);

    init_idt_entry(idt, 0xe0, int_e0);
    init_idt_entry(idt, 0xe1, int_e1);
    init_idt_entry(idt, 0xe2, int_e2);
    init_idt_entry(idt, 0xe3, int_e3);
    init_idt_entry(idt, 0xe4, int_e4);
    init_idt_entry(idt, 0xe5, int_e5);
    init_idt_entry(idt, 0xe6, int_e6);
    init_idt_entry(idt, 0xe7, int_e7);
    init_idt_entry(idt, 0xe8, int_e8);
    init_idt_entry(idt, 0xe9, int_e9);
    init_idt_entry(idt, 0xea, int_ea);
    init_idt_entry(idt, 0xeb, int_eb);
    init_idt_entry(idt, 0xec, int_ec);
    init_idt_entry(idt, 0xed, int_ed);
    init_idt_entry(idt, 0xee, int_ee);
    init_idt_entry(idt, 0xef, int_ef);

    init_idt_entry(idt, 0xf0, int_f0);
    init_idt_entry(idt, 0xf1, int_f1);
    init_idt_entry(idt, 0xf2, int_f2);
    init_idt_entry(idt, 0xf3, int_f3);
    init_idt_entry(idt, 0xf4, int_f4);
    init_idt_entry(idt, 0xf5, int_f5);
    init_idt_entry(idt, 0xf6, int_f6);
    init_idt_entry(idt, 0xf7, int_f7);
    init_idt_entry(idt, 0xf8, int_f8);
    init_idt_entry(idt, 0xf9, int_f9);
    init_idt_entry(idt, 0xfa, int_fa);
    init_idt_entry(idt, 0xfb, int_fb);
    init_idt_entry(idt, 0xfc, int_fc);
    init_idt_entry(idt, 0xfd, int_fd);
    init_idt_entry(idt, 0xfe, int_fe);
    init_idt_entry(idt, 0xff, int_ff);
}

BOOT_CODE bool_t
map_kernel_window(
    pdpte_t*   pdpt,
    pde_t*     pd,
    pte_t*     pt,
    p_region_t ndks_p_reg
#ifdef CONFIG_IRQ_IOAPIC
    , uint32_t num_ioapic,
    paddr_t*   ioapic_paddrs
#endif
#ifdef CONFIG_IOMMU
    , uint32_t   num_drhu,
    paddr_t*   drhu_list
#endif
)
{
    paddr_t  phys;
    uint32_t idx;
    pde_t    pde;
    pte_t    pte;
    unsigned int UNUSED i;

    if ((void*)pdpt != (void*)pd) {
        for (idx = 0; idx < BIT(PDPT_BITS); idx++) {
            pdpte_ptr_new(pdpt + idx,
                          pptr_to_paddr(pd + (idx * BIT(PD_BITS))),
                          0, /* avl*/
                          0, /* cache_disabled */
                          0, /* write_through */
                          1  /* present */
                         );
        }
    }

    /* Mapping of PPTR_BASE (virtual address) to kernel's PADDR_BASE
     * up to end of virtual address space except for the last large page.
     */
    phys = PADDR_BASE;
    idx = PPTR_BASE >> LARGE_PAGE_BITS;

#if CONFIG_MAX_NUM_TRACE_POINTS > 0
    /* steal the last large for logging */
    while (idx < BIT(PD_BITS + PDPT_BITS) - 2) {
#else
    while (idx < BIT(PD_BITS + PDPT_BITS) - 1) {
#endif /* CONFIG_MAX_NUM_TRACE_POINTS > 0 */
        pde = pde_pde_large_new(
                  phys,   /* page_base_address    */
                  0,      /* pat                  */
                  0,      /* avl                  */
                  1,      /* global               */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  1,      /* read_write           */
                  1       /* present              */
              );
        pd[idx] = pde;
        phys += BIT(LARGE_PAGE_BITS);
        idx++;
    }

    /* crosscheck whether we have mapped correctly so far */
    assert(phys == PADDR_TOP);

#if CONFIG_MAX_NUM_TRACE_POINTS > 0
    /* mark the address of the log. We will map it
        * in later with the correct attributes, but we need
        * to wait until we can call alloc_region. */
    ksLog = (ks_log_entry_t *) paddr_to_pptr(phys);
    phys += BIT(LARGE_PAGE_BITS);
    assert(idx == IA32_KSLOG_IDX);
    idx++;
#endif /* CONFIG_MAX_NUM_TRACE_POINTS > 0 */

    /* map page table of last 4M of virtual address space to page directory */
    pde = pde_pde_small_new(
              pptr_to_paddr(pt), /* pt_base_address  */
              0,                 /* avl              */
              0,                 /* accessed         */
              0,                 /* cache_disabled   */
              0,                 /* write_through    */
              1,                 /* super_user       */
              1,                 /* read_write       */
              1                  /* present          */
          );
    pd[idx] = pde;

    /* Start with an empty guard page preceding the stack. */
    idx = 0;
    pte = pte_new(
              0,      /* page_base_address    */
              0,      /* avl                  */
              0,      /* global               */
              0,      /* pat                  */
              0,      /* dirty                */
              0,      /* accessed             */
              0,      /* cache_disabled       */
              0,      /* write_through        */
              0,      /* super_user           */
              0,      /* read_write           */
              0       /* present              */
          );
    pt[idx] = pte;
    idx++;

    /* establish NDKS (node kernel state) mappings in page table */
    phys = ndks_p_reg.start;
    while (idx - 1 < (ndks_p_reg.end - ndks_p_reg.start) >> PAGE_BITS) {
        pte = pte_new(
                  phys,   /* page_base_address    */
                  0,      /* avl                  */
                  1,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  1,      /* read_write           */
                  1       /* present              */
              );
        pt[idx] = pte;
        phys += BIT(PAGE_BITS);
        idx++;
    }

    /* null mappings up to PPTR_KDEV */

    while (idx < (PPTR_KDEV & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS) {
        pte = pte_new(
                  0,      /* page_base_address    */
                  0,      /* avl                  */
                  0,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  0,      /* read_write           */
                  0       /* present              */
              );
        pt[idx] = pte;
        phys += BIT(PAGE_BITS);
        idx++;
    }

    /* map kernel devices (devices only used by the kernel) */

    /* map kernel devices: APIC */
    phys = apic_get_base_paddr();
    if (!phys) {
        return false;
    }
    pte = pte_new(
              phys,   /* page_base_address    */
              0,      /* avl                  */
              1,      /* global               */
              0,      /* pat                  */
              0,      /* dirty                */
              0,      /* accessed             */
              1,      /* cache_disabled       */
              1,      /* write_through        */
              0,      /* super_user           */
              1,      /* read_write           */
              1       /* present              */
          );

    assert(idx == (PPTR_APIC & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS);
    pt[idx] = pte;
    idx++;

#ifdef CONFIG_IRQ_IOAPIC
    for (i = 0; i < num_ioapic; i++) {
        phys = ioapic_paddrs[i];
        pte = pte_new(
                  phys,   /* page_base_address    */
                  0,      /* avl                  */
                  1,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  1,      /* cache_disabled       */
                  1,      /* write_through        */
                  0,      /* super_user           */
                  1,      /* read_write           */
                  1       /* present              */
              );
        assert(idx == ( (PPTR_IOAPIC_START + i * BIT(PAGE_BITS)) & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS);
        pt[idx] = pte;
        idx++;
        if (idx == BIT(PT_BITS)) {
            return false;
        }
    }
    /* put in null mappings for any extra IOAPICs */
    for (; i < CONFIG_MAX_NUM_IOAPIC; i++) {
        pte = pte_new(
                  0,      /* page_base_address    */
                  0,      /* avl                  */
                  0,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  0,      /* read_write           */
                  0       /* present              */
              );
        assert(idx == ( (PPTR_IOAPIC_START + i * BIT(PAGE_BITS)) & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS);
        pt[idx] = pte;
        idx++;
    }
#endif

#ifdef CONFIG_IOMMU
    /* map kernel devices: IOMMUs */
    for (i = 0; i < num_drhu; i++) {
        phys = (paddr_t)drhu_list[i];
        pte = pte_new(
                  phys,   /* page_base_address    */
                  0,      /* avl                  */
                  1,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  1,      /* cache_disabled       */
                  1,      /* write_through        */
                  0,      /* super_user           */
                  1,      /* read_write           */
                  1       /* present              */
              );

        assert(idx == ((PPTR_DRHU_START + i * BIT(PAGE_BITS)) & MASK(LARGE_PAGE_BITS)) >> PAGE_BITS);
        pt[idx] = pte;
        idx++;
        if (idx == BIT(PT_BITS)) {
            return false;
        }
    }
#endif

    /* mark unused kernel-device pages as 'not present' */
    while (idx < BIT(PT_BITS)) {
        pte = pte_new(
                  0,      /* page_base_address    */
                  0,      /* avl                  */
                  0,      /* global               */
                  0,      /* pat                  */
                  0,      /* dirty                */
                  0,      /* accessed             */
                  0,      /* cache_disabled       */
                  0,      /* write_through        */
                  0,      /* super_user           */
                  0,      /* read_write           */
                  0       /* present              */
              );
        pt[idx] = pte;
        idx++;
    }

    /* Check we haven't added too many kernel-device mappings.*/
    assert(idx == BIT(PT_BITS));

    invalidatePageStructureCache();
    return true;
}

/* Note: this function will invalidate any pointers previously returned from this function */
BOOT_CODE void*
map_temp_boot_page(void* entry, uint32_t large_pages)
{
    void* replacement_vaddr;
    unsigned int i;
    unsigned int offset_in_page;

    unsigned int phys_pg_start = (unsigned int)(entry) & ~MASK(LARGE_PAGE_BITS);
    unsigned int virt_pd_start = (PPTR_BASE >> LARGE_PAGE_BITS) - large_pages;
    unsigned int virt_pg_start = PPTR_BASE - (large_pages << LARGE_PAGE_BITS);

    for (i = 0; i < large_pages; ++i) {
        unsigned int pg_offset = i << LARGE_PAGE_BITS; // num pages since start * page size

        pde_pde_large_ptr_new(get_boot_pd() + virt_pd_start + i,
                              phys_pg_start + pg_offset, /* physical address */
                              0, /* pat            */
                              0, /* avl            */
                              1, /* global         */
                              0, /* dirty          */
                              0, /* accessed       */
                              0, /* cache_disabled */
                              0, /* write_through  */
                              0, /* super_user     */
                              1, /* read_write     */
                              1  /* present        */
                             );
        invalidateTLBentry(virt_pg_start + pg_offset);
    }

    // assign replacement virtual addresses page
    offset_in_page = (unsigned int)(entry) & MASK(LARGE_PAGE_BITS);
    replacement_vaddr = (void*)(virt_pg_start + offset_in_page);

    invalidatePageStructureCache();

    return replacement_vaddr;
}

BOOT_CODE bool_t
init_vm_state(pdpte_t *kernel_pdpt, pde_t* kernel_pd, pte_t* kernel_pt)
{
    ia32KScacheLineSizeBits = getCacheLineSizeBits();
    if (!ia32KScacheLineSizeBits) {
        return false;
    }
    ia32KSkernelPDPT = kernel_pdpt;
    ia32KSkernelPD = kernel_pd;
    ia32KSkernelPT = kernel_pt;
    init_tss(&ia32KStss);
    init_gdt(ia32KSgdt, &ia32KStss);
    init_idt(ia32KSidt);
    return true;
}

/* initialise CPU's descriptor table registers (GDTR, IDTR, LDTR, TR) */

BOOT_CODE void
init_dtrs(void)
{
    /* setup the GDT pointer and limit and load into GDTR */
    gdt_idt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
    gdt_idt_ptr.basel = (uint32_t)ia32KSgdt;
    gdt_idt_ptr.baseh = (uint16_t)((uint32_t)ia32KSgdt >> 16);
    ia32_install_gdt(&gdt_idt_ptr);

    /* setup the IDT pointer and limit and load into IDTR */
    gdt_idt_ptr.limit = (sizeof(idt_entry_t) * (int_max + 1)) - 1;
    gdt_idt_ptr.basel = (uint32_t)ia32KSidt;
    gdt_idt_ptr.baseh = (uint16_t)((uint32_t)ia32KSidt >> 16);
    ia32_install_idt(&gdt_idt_ptr);

    /* load NULL LDT selector into LDTR */
    ia32_install_ldt(SEL_NULL);

    /* load TSS selector into Task Register (TR) */
    ia32_install_tss(SEL_TSS);
}

BOOT_CODE void
write_it_asid_pool(cap_t it_ap_cap, cap_t it_vspace_cap)
{
    asid_pool_t* ap = ASID_POOL_PTR(pptr_of_cap(it_ap_cap));
    ap->array[IT_ASID] = PDE_PTR(pptr_of_cap(it_vspace_cap));
    ia32KSASIDTable[IT_ASID >> asidLowBits] = ap;
}

BOOT_CODE bool_t
init_pat_msr(void)
{
    ia32_pat_msr_t pat_msr;
    /* First verify PAT is supported by the machine.
     *      See section 11.12.1 of Volume 3 of the Intel manual */
    if ( (ia32_cpuid_edx(0x1, 0x0) & BIT(16)) == 0) {
        printf("PAT support not found\n");
        return false;
    }
    pat_msr.words[0] = ia32_rdmsr_low(IA32_PAT_MSR);
    pat_msr.words[1] = ia32_rdmsr_high(IA32_PAT_MSR);
    /* Set up the PAT MSR to the Intel defaults, just in case
     * they have been changed but a bootloader somewhere along the way */
    ia32_pat_msr_ptr_set_pa0(&pat_msr, IA32_PAT_MT_WRITE_BACK);
    ia32_pat_msr_ptr_set_pa1(&pat_msr, IA32_PAT_MT_WRITE_THROUGH);
    ia32_pat_msr_ptr_set_pa2(&pat_msr, IA32_PAT_MT_UNCACHED);
    ia32_pat_msr_ptr_set_pa3(&pat_msr, IA32_PAT_MT_UNCACHEABLE);
    /* Add the WriteCombining cache type to the PAT */
    ia32_pat_msr_ptr_set_pa4(&pat_msr, IA32_PAT_MT_WRITE_COMBINING);
    ia32_wrmsr(IA32_PAT_MSR, pat_msr.words[1], pat_msr.words[0]);
    return true;
}

/* ==================== BOOT CODE FINISHES HERE ==================== */

static uint32_t CONST WritableFromVMRights(vm_rights_t vm_rights)
{
    switch (vm_rights) {
    case VMReadOnly:
        return 0;

    case VMKernelOnly:
    case VMReadWrite:
        return 1;

    default:
        fail("Invalid VM rights");
    }
}

static uint32_t CONST SuperUserFromVMRights(vm_rights_t vm_rights)
{
    switch (vm_rights) {
    case VMKernelOnly:
        return 0;

    case VMReadOnly:
    case VMReadWrite:
        return 1;

    default:
        fail("Invalid VM rights");
    }
}

static pde_t CONST makeUserPDE(paddr_t paddr, vm_attributes_t vm_attr, vm_rights_t vm_rights)
{
    return pde_pde_large_new(
               paddr,                                          /* page_base_address    */
               vm_attributes_get_ia32PATBit(vm_attr),          /* pat                  */
               0,                                              /* avl                  */
               0,                                              /* global               */
               0,                                              /* dirty                */
               0,                                              /* accessed             */
               vm_attributes_get_ia32PCDBit(vm_attr),          /* cache_disabled       */
               vm_attributes_get_ia32PWTBit(vm_attr),          /* write_through        */
               SuperUserFromVMRights(vm_rights),               /* super_user           */
               WritableFromVMRights(vm_rights),                /* read_write           */
               1                                               /* present              */
           );
}

static pte_t CONST makeUserPTE(paddr_t paddr, vm_attributes_t vm_attr, vm_rights_t vm_rights)
{
    return pte_new(
               paddr,                                          /* page_base_address    */
               0,                                              /* avl                  */
               0,                                              /* global               */
               vm_attributes_get_ia32PATBit(vm_attr),          /* pat                  */
               0,                                              /* dirty                */
               0,                                              /* accessed             */
               vm_attributes_get_ia32PCDBit(vm_attr),          /* cache_disabled       */
               vm_attributes_get_ia32PWTBit(vm_attr),          /* write_through        */
               SuperUserFromVMRights(vm_rights),               /* super_user           */
               WritableFromVMRights(vm_rights),                /* read_write           */
               1                                               /* present              */
           );
}

word_t* PURE lookupIPCBuffer(bool_t isReceiver, tcb_t *thread)
{
    word_t      w_bufferPtr;
    cap_t       bufferCap;
    vm_rights_t vm_rights;

    w_bufferPtr = thread->tcbIPCBuffer;
    bufferCap = TCB_PTR_CTE_PTR(thread, tcbBuffer)->cap;

    if (cap_get_capType(bufferCap) != cap_frame_cap) {
        return NULL;
    }

    vm_rights = cap_frame_cap_get_capFVMRights(bufferCap);
    if (vm_rights == VMReadWrite || (!isReceiver && vm_rights == VMReadOnly)) {
        word_t basePtr;
        unsigned int pageBits;

        basePtr = cap_frame_cap_get_capFBasePtr(bufferCap);
        pageBits = pageBitsForSize(cap_frame_cap_get_capFSize(bufferCap));
        return (word_t *)(basePtr + (w_bufferPtr & MASK(pageBits)));
    } else {
        return NULL;
    }
}

static lookupPTSlot_ret_t lookupPTSlot(void *vspace, vptr_t vptr)
{
    lookupPTSlot_ret_t ret;
    lookupPDSlot_ret_t pdSlot;

    pdSlot = lookupPDSlot(vspace, vptr);
    if (pdSlot.status != EXCEPTION_NONE) {
        ret.ptSlot = NULL;
        ret.status = pdSlot.status;
        return ret;
    }

    if ((pde_ptr_get_page_size(pdSlot.pdSlot) != pde_pde_small) ||
            !pde_pde_small_ptr_get_present(pdSlot.pdSlot)) {
        current_lookup_fault = lookup_fault_missing_capability_new(PAGE_BITS + PT_BITS);

        ret.ptSlot = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    } else {
        pte_t* pt;
        pte_t* ptSlot;
        unsigned int ptIndex;

        pt = paddr_to_pptr(pde_pde_small_ptr_get_pt_base_address(pdSlot.pdSlot));
        ptIndex = (vptr >> PAGE_BITS) & MASK(PT_BITS);
        ptSlot = pt + ptIndex;

        ret.ptSlot = ptSlot;
        ret.status = EXCEPTION_NONE;
        return ret;
    }
}

exception_t handleVMFault(tcb_t* thread, vm_fault_type_t vm_faultType)
{
    uint32_t addr;
    uint32_t fault;

    addr = getFaultAddr();
    fault = getRegister(thread, Error);

    switch (vm_faultType) {
    case IA32DataFault:
        current_fault = fault_vm_fault_new(addr, fault, false);
        return EXCEPTION_FAULT;

    case IA32InstructionFault:
        current_fault = fault_vm_fault_new(addr, fault, true);
        return EXCEPTION_FAULT;

    default:
        fail("Invalid VM fault type");
    }
}

exception_t checkValidIPCBuffer(vptr_t vptr, cap_t cap)
{
    if (cap_get_capType(cap) != cap_frame_cap) {
        userError("IPC Buffer is an invalid cap.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (!IS_ALIGNED(vptr, 9)) {
        userError("IPC Buffer vaddr 0x%x is not aligned.", (int)vptr);
        current_syscall_error.type = seL4_AlignmentError;
        return EXCEPTION_SYSCALL_ERROR;
    }

    return EXCEPTION_NONE;
}

vm_rights_t CONST maskVMRights(vm_rights_t vm_rights, cap_rights_t cap_rights_mask)
{
    if (vm_rights == VMReadOnly && cap_rights_get_capAllowRead(cap_rights_mask)) {
        return VMReadOnly;
    }
    if (vm_rights == VMReadWrite && cap_rights_get_capAllowRead(cap_rights_mask)) {
        if (!cap_rights_get_capAllowWrite(cap_rights_mask)) {
            return VMReadOnly;
        } else {
            return VMReadWrite;
        }
    }
    return VMKernelOnly;
}

static void flushTable(void *vspace, word_t vptr, pte_t* pt)
{
    unsigned int i;
    cap_t        threadRoot;

    assert(IS_ALIGNED(vptr, PT_BITS + PAGE_BITS));

    /* check if page table belongs to current address space */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == vspace) {
        /* find valid mappings */
        for (i = 0; i < BIT(PT_BITS); i++) {
            if (pte_get_present(pt[i])) {
                invalidateTLBentry(vptr + (i << PAGE_BITS));
            }
        }
    }
}

static findVSpaceForASID_ret_t findVSpaceForASID(asid_t asid)
{
    findVSpaceForASID_ret_t ret;
    asid_pool_t*        poolPtr;
    void*               vspace_root;

    poolPtr = ia32KSASIDTable[asid >> asidLowBits];
    if (!poolPtr) {
        current_lookup_fault = lookup_fault_invalid_root_new();

        ret.vspace_root = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    vspace_root = poolPtr->array[asid & MASK(asidLowBits)];
    if (!vspace_root) {
        current_lookup_fault = lookup_fault_invalid_root_new();

        ret.vspace_root = NULL;
        ret.status = EXCEPTION_LOOKUP_FAULT;
        return ret;
    }

    ret.vspace_root = vspace_root;
    ret.status = EXCEPTION_NONE;
    return ret;
}

void setVMRoot(tcb_t* tcb)
{
    cap_t               threadRoot;
    void *vspace_root;
    asid_t              asid;
    findVSpaceForASID_ret_t find_ret;

    threadRoot = TCB_PTR_CTE_PTR(tcb, tcbVTable)->cap;

    vspace_root = getValidNativeRoot(threadRoot);
    if (!vspace_root) {
        setCurrentPD(pptr_to_paddr(ia32KSkernelPDPT));
        return;
    }

    asid = cap_get_capMappedASID(threadRoot);
    find_ret = findVSpaceForASID(asid);
    if (find_ret.status != EXCEPTION_NONE || find_ret.vspace_root != vspace_root) {
        setCurrentPD(pptr_to_paddr(ia32KSkernelPDPT));
        return;
    }

    /* only set PD if we change it, otherwise we flush the TLB needlessly */
    if (getCurrentPD() != pptr_to_paddr(vspace_root)) {
        setCurrentPD(pptr_to_paddr(vspace_root));
    }
}

void deleteASIDPool(asid_t asid_base, asid_pool_t* pool)
{
    /* Haskell error: "ASID pool's base must be aligned" */
    assert(IS_ALIGNED(asid_base, asidLowBits));

    if (ia32KSASIDTable[asid_base >> asidLowBits] == pool) {
        ia32KSASIDTable[asid_base >> asidLowBits] = NULL;
        setVMRoot(ksCurThread);
    }
}

void deleteASID(asid_t asid, void* vspace)
{
    asid_pool_t* poolPtr;

    poolPtr = ia32KSASIDTable[asid >> asidLowBits];

    if (poolPtr != NULL && poolPtr->array[asid & MASK(asidLowBits)] == vspace) {
        poolPtr->array[asid & MASK(asidLowBits)] = NULL;
        setVMRoot(ksCurThread);
    }
}

void unmapPageTable(asid_t asid, vptr_t vaddr, pte_t* pt)
{
    findVSpaceForASID_ret_t find_ret;
    lookupPDSlot_ret_t    lu_ret;

    find_ret = findVSpaceForASID(asid);
    if (find_ret.status != EXCEPTION_NONE) {
        return;
    }

    lu_ret = lookupPDSlot(find_ret.vspace_root, vaddr);
    if (lu_ret.status != EXCEPTION_NONE) {
        return;
    }

    flushTable(find_ret.vspace_root, vaddr, pt);

    *lu_ret.pdSlot = pde_pde_small_new(
                         0,  /* pt_base_address  */
                         0,  /* avl              */
                         0,  /* accessed         */
                         0,  /* cache_disabled   */
                         0,  /* write_through    */
                         0,  /* super_user       */
                         0,  /* read_write       */
                         0   /* present          */
                     );
    invalidatePageStructureCache();
}

void unmapPage(vm_page_size_t page_size, asid_t asid, vptr_t vptr, void *pptr)
{
    findVSpaceForASID_ret_t find_ret;
    lookupPTSlot_ret_t  lu_ret;
    cap_t               threadRoot;
    lookupPDSlot_ret_t  pd_ret;
    pde_t               *pde;

    find_ret = findVSpaceForASID(asid);
    if (find_ret.status != EXCEPTION_NONE) {
        return;
    }

    /* check if page belongs to current address space */
    threadRoot = TCB_PTR_CTE_PTR(ksCurThread, tcbVTable)->cap;
    if (isValidNativeRoot(threadRoot) && (void*)pptr_of_cap(threadRoot) == find_ret.vspace_root) {
        invalidateTLBentry(vptr);
    }

    switch (page_size) {
    case IA32_SmallPage:
        lu_ret = lookupPTSlot(find_ret.vspace_root, vptr);
        if (lu_ret.status != EXCEPTION_NONE) {
            return;
        }
        if (! (pte_ptr_get_present(lu_ret.ptSlot)
                && (pte_ptr_get_page_base_address(lu_ret.ptSlot)
                    == pptr_to_paddr(pptr)))) {
            return;
        }
        *lu_ret.ptSlot = pte_new(
                             0,      /* page_base_address    */
                             0,      /* avl                  */
                             0,      /* global               */
                             0,      /* pat                  */
                             0,      /* dirty                */
                             0,      /* accessed             */
                             0,      /* cache_disabled       */
                             0,      /* write_through        */
                             0,      /* super_user           */
                             0,      /* read_write           */
                             0       /* present              */
                         );
        break;

    case IA32_LargePage:
        pd_ret = lookupPDSlot(find_ret.vspace_root, vptr);
        if (pd_ret.status != EXCEPTION_NONE) {
            return;
        }
        pde = pd_ret.pdSlot;
        if (! (pde_ptr_get_page_size(pde) == pde_pde_large
                && pde_pde_large_ptr_get_present(pde)
                && (pde_pde_large_ptr_get_page_base_address(pde)
                    == pptr_to_paddr(pptr)))) {
            return;
        }
        *pde = pde_pde_large_new(
                   0,      /* page_base_address    */
                   0,      /* pat                  */
                   0,      /* avl                  */
                   0,      /* global               */
                   0,      /* dirty                */
                   0,      /* accessed             */
                   0,      /* cache_disabled       */
                   0,      /* write_through        */
                   0,      /* super_user           */
                   0,      /* read_write           */
                   0       /* present              */
               );
        break;

    default:
        fail("Invalid page type");
    }
    invalidatePageStructureCache();
}

static exception_t performASIDControlInvocation(void* frame, cte_t* slot, cte_t* parent, asid_t asid_base)
{
    memzero(frame, 1 << pageBitsForSize(IA32_SmallPage));
    cteInsert(
        cap_asid_pool_cap_new(
            asid_base,          /* capASIDBase  */
            WORD_REF(frame)     /* capASIDPool  */
        ),
        parent,
        slot
    );
    /* Haskell error: "ASID pool's base must be aligned" */
    assert((asid_base & MASK(asidLowBits)) == 0);
    ia32KSASIDTable[asid_base >> asidLowBits] = (asid_pool_t*)frame;

    return EXCEPTION_NONE;
}

static exception_t
performPageGetAddress(void *vbase_ptr)
{
    paddr_t capFBasePtr;

    /* Get the physical address of this frame. */
    capFBasePtr = pptr_to_paddr(vbase_ptr);

    /* return it in the first message register */
    setRegister(ksCurThread, msgRegisters[0], capFBasePtr);
    setRegister(ksCurThread, msgInfoRegister,
                wordFromMessageInfo(message_info_new(0, 0, 0, 1)));

    return EXCEPTION_NONE;
}

static inline bool_t
checkVPAlignment(vm_page_size_t sz, word_t w)
{
    return IS_ALIGNED(w, pageBitsForSize(sz));
}

static exception_t
decodeIA32PageTableInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte, cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    word_t          vaddr;
    vm_attributes_t attr;
    lookupPDSlot_ret_t pdSlot;
    cap_t           vspaceCap;
    void*           vspace;
    pde_t           pde;
    paddr_t         paddr;
    asid_t          asid;

    if (label == IA32PageTableUnmap) {
        if (! isFinalCapability(cte)) {
            current_syscall_error.type = seL4_RevokeFirst;
            userError("IA32PageTable: Cannot unmap if more than one cap exists.");
            return EXCEPTION_SYSCALL_ERROR;
        }
        setThreadState(ksCurThread, ThreadState_Restart);

        if (cap_page_table_cap_get_capPTIsMapped(cap)) {
            pte_t *pt = PTE_PTR(cap_page_table_cap_get_capPTBasePtr(cap));
            unmapPageTable(
                cap_page_table_cap_get_capPTMappedASID(cap),
                cap_page_table_cap_get_capPTMappedAddress(cap),
                pt
            );
            clearMemory((void *)pt, cap_get_capSizeBits(cap));
        }
        cap_page_table_cap_ptr_set_capPTIsMapped(&(cte->cap), 0);

        return EXCEPTION_NONE;
    }

    if (label != IA32PageTableMap ) {
        userError("IA32PageTable: Illegal operation.");
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (length < 2 || extraCaps.excaprefs[0] == NULL) {
        userError("IA32PageTable: Truncated message.");
        current_syscall_error.type = seL4_TruncatedMessage;
        return EXCEPTION_SYSCALL_ERROR;
    }

    if (cap_page_table_cap_get_capPTIsMapped(cap)) {
        userError("IA32PageTable: Page table is already mapped to a page directory.");
        current_syscall_error.type =
            seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vaddr = getSyscallArg(0, buffer) & (~MASK(PT_BITS + PAGE_BITS));
    attr = vmAttributesFromWord(getSyscallArg(1, buffer));
    vspaceCap = extraCaps.excaprefs[0]->cap;

    if (!isValidNativeRoot(vspaceCap)) {
        current_syscall_error.type = seL4_InvalidCapability;
        current_syscall_error.invalidCapNumber = 1;

        return EXCEPTION_SYSCALL_ERROR;
    }

    vspace = (void*)pptr_of_cap(vspaceCap);
    asid = cap_get_capMappedASID(vspaceCap);

    if (vaddr >= PPTR_USER_TOP) {
        userError("IA32PageTable: Mapping address too high.");
        current_syscall_error.type = seL4_InvalidArgument;
        current_syscall_error.invalidArgumentNumber = 0;

        return EXCEPTION_SYSCALL_ERROR;
    }

    {
        findVSpaceForASID_ret_t find_ret;

        find_ret = findVSpaceForASID(asid);
        if (find_ret.status != EXCEPTION_NONE) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;

            return EXCEPTION_SYSCALL_ERROR;
        }

        if (find_ret.vspace_root != vspace) {
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 1;

            return EXCEPTION_SYSCALL_ERROR;
        }
    }

    pdSlot = lookupPDSlot(vspace, vaddr);
    if (pdSlot.status != EXCEPTION_NONE) {
        current_syscall_error.type = seL4_FailedLookup;
        current_syscall_error.failedLookupWasSource = false;

        return EXCEPTION_SYSCALL_ERROR;
    }

    if (((pde_ptr_get_page_size(pdSlot.pdSlot) == pde_pde_small) && pde_pde_small_ptr_get_present(pdSlot.pdSlot)) ||
            ((pde_ptr_get_page_size(pdSlot.pdSlot) == pde_pde_large) && pde_pde_large_ptr_get_present(pdSlot.pdSlot))) {
        current_syscall_error.type = seL4_DeleteFirst;

        return EXCEPTION_SYSCALL_ERROR;
    }

    paddr = pptr_to_paddr(PTE_PTR(cap_page_table_cap_get_capPTBasePtr(cap)));
    pde = pde_pde_small_new(
              paddr,                                      /* pt_base_address  */
              0,                                          /* avl              */
              0,                                          /* accessed         */
              vm_attributes_get_ia32PCDBit(attr),      /* cache_disabled   */
              vm_attributes_get_ia32PWTBit(attr),      /* write_through    */
              1,                                          /* super_user       */
              1,                                          /* read_write       */
              1                                           /* present          */
          );

    cap = cap_page_table_cap_set_capPTIsMapped(cap, 1);
    cap = cap_page_table_cap_set_capPTMappedASID(cap, asid);
    cap = cap_page_table_cap_set_capPTMappedAddress(cap, vaddr);

    cte->cap = cap;
    *pdSlot.pdSlot = pde;

    setThreadState(ksCurThread, ThreadState_Restart);
    invalidatePageStructureCache();
    return EXCEPTION_NONE;
}

static exception_t
decodeIA32FrameInvocation(
    word_t label,
    unsigned int length,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    switch (label) {
    case IA32PageMap: { /* Map */
        word_t          vaddr;
        word_t          vtop;
        word_t          w_rightsMask;
        paddr_t         paddr;
        cap_t           vspaceCap;
        void*           vspace;
        vm_rights_t     capVMRights;
        vm_rights_t     vmRights;
        vm_attributes_t vmAttr;
        vm_page_size_t  frameSize;
        asid_t          asid;

        if (length < 3 || extraCaps.excaprefs[0] == NULL) {
            current_syscall_error.type = seL4_TruncatedMessage;

            return EXCEPTION_SYSCALL_ERROR;
        }

        frameSize = cap_frame_cap_get_capFSize(cap);
        vaddr = getSyscallArg(0, buffer) & (~MASK(pageBitsForSize(frameSize)));
        w_rightsMask = getSyscallArg(1, buffer);
        vmAttr = vmAttributesFromWord(getSyscallArg(2, buffer));
        vspaceCap = extraCaps.excaprefs[0]->cap;

        capVMRights = cap_frame_cap_get_capFVMRights(cap);

        if (cap_frame_cap_get_capFMappedASID(cap) != asidInvalid) {
            userError("IA32Frame: Frame already mapped.");
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;

            return EXCEPTION_SYSCALL_ERROR;
        }

        if (!isValidNativeRoot(vspaceCap)) {
            userError("IA32Frame: Attempting to map frame into invalid page directory cap.");
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 1;

            return EXCEPTION_SYSCALL_ERROR;
        }
        vspace = (void*)pptr_of_cap(vspaceCap);
        asid = cap_get_capMappedASID(vspaceCap);

        {
            findVSpaceForASID_ret_t find_ret;

            find_ret = findVSpaceForASID(asid);
            if (find_ret.status != EXCEPTION_NONE) {
                current_syscall_error.type = seL4_FailedLookup;
                current_syscall_error.failedLookupWasSource = false;

                return EXCEPTION_SYSCALL_ERROR;
            }

            if (find_ret.vspace_root != vspace) {
                current_syscall_error.type = seL4_InvalidCapability;
                current_syscall_error.invalidCapNumber = 1;

                return EXCEPTION_SYSCALL_ERROR;
            }
        }

        vtop = vaddr + BIT(pageBitsForSize(frameSize));

        if (vtop > PPTR_USER_TOP) {
            userError("IA32Frame: Mapping address too high.");
            current_syscall_error.type = seL4_InvalidArgument;
            current_syscall_error.invalidArgumentNumber = 0;

            return EXCEPTION_SYSCALL_ERROR;
        }

        vmRights = maskVMRights(capVMRights, rightsFromWord(w_rightsMask));

        if (!checkVPAlignment(frameSize, vaddr)) {
            current_syscall_error.type = seL4_AlignmentError;

            return EXCEPTION_SYSCALL_ERROR;
        }

        paddr = pptr_to_paddr((void*)cap_frame_cap_get_capFBasePtr(cap));

        cap = cap_frame_cap_set_capFMappedASID(cap, asid);
        cap = cap_frame_cap_set_capFMappedAddress(cap, vaddr);

        switch (frameSize) {
        /* PTE mappings */
        case IA32_SmallPage: {
            pte_t              pte;
            lookupPTSlot_ret_t lu_ret;

            lu_ret = lookupPTSlot(vspace, vaddr);
            if (lu_ret.status != EXCEPTION_NONE) {
                current_syscall_error.type = seL4_FailedLookup;
                current_syscall_error.failedLookupWasSource = false;
                /* current_lookup_fault will have been set by lookupPTSlot */
                return EXCEPTION_SYSCALL_ERROR;
            }

            pte = makeUserPTE(paddr, vmAttr, vmRights);
            cte->cap = cap;
            *lu_ret.ptSlot = pte;

            break;
        }

        /* PDE mappings */
        case IA32_LargePage: {
            pde_t* pdeSlot;
            lookupPDSlot_ret_t lu_ret;

            lu_ret = lookupPDSlot(vspace, vaddr);
            if (lu_ret.status != EXCEPTION_NONE) {
                current_syscall_error.type = seL4_FailedLookup;
                current_syscall_error.failedLookupWasSource = false;
                /* current_lookup_fault will have been set by lookupPDSlot */
                return EXCEPTION_SYSCALL_ERROR;
            }
            pdeSlot = lu_ret.pdSlot;

            if ((pde_ptr_get_page_size(pdeSlot) == pde_pde_small) &&
                    (pde_pde_small_ptr_get_present(pdeSlot))) {
                current_syscall_error.type = seL4_DeleteFirst;

                return EXCEPTION_SYSCALL_ERROR;
            }

            *pdeSlot = makeUserPDE(paddr, vmAttr, vmRights);
            cte->cap = cap;

            break;
        }

        default:
            fail("Invalid page type");
        }
        invalidatePageStructureCache();
        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

    case IA32PageRemap: { /* Remap */
        word_t          vaddr;
        word_t          w_rightsMask;
        paddr_t         paddr;
        cap_t           vspaceCap;
        void*           vspace;
        vm_rights_t     capVMRights;
        vm_rights_t     vmRights;
        vm_attributes_t vmAttr;
        vm_page_size_t  frameSize;
        asid_t          asid;

#ifdef CONFIG_IOMMU
        if (cap_frame_cap_get_capFIsIOSpace(cap)) {
            userError("IA32FrameRemap: Attempting to remap frame mapped into an IOSpace");
            current_syscall_error.type = seL4_IllegalOperation;

            return EXCEPTION_SYSCALL_ERROR;
        }
#endif

        if (length < 2 || extraCaps.excaprefs[0] == NULL) {
            userError("IA32FrameRemap: Truncated message");
            current_syscall_error.type = seL4_TruncatedMessage;

            return EXCEPTION_SYSCALL_ERROR;
        }

        w_rightsMask = getSyscallArg(0, buffer);
        vmAttr = vmAttributesFromWord(getSyscallArg(1, buffer));
        vspaceCap = extraCaps.excaprefs[0]->cap;

        if (!isValidNativeRoot(vspaceCap)) {
            userError("IA32FrameRemap: Attempting to map frame into invalid page directory.");
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 1;

            return EXCEPTION_SYSCALL_ERROR;
        }
        vspace = (void*)pptr_of_cap(vspaceCap);
        asid = cap_get_capMappedASID(vspaceCap);

        if (cap_frame_cap_get_capFMappedASID(cap) == asidInvalid) {
            userError("IA32PageRemap: Frame must already have been mapped.");
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;

            return EXCEPTION_SYSCALL_ERROR;
        }

        {
            findVSpaceForASID_ret_t find_ret;

            find_ret = findVSpaceForASID(asid);
            if (find_ret.status != EXCEPTION_NONE) {
                current_syscall_error.type = seL4_FailedLookup;
                current_syscall_error.failedLookupWasSource = false;

                return EXCEPTION_SYSCALL_ERROR;
            }

            if (find_ret.vspace_root != vspace) {
                current_syscall_error.type = seL4_InvalidCapability;
                current_syscall_error.invalidCapNumber = 1;

                return EXCEPTION_SYSCALL_ERROR;
            }
        }

        vaddr       = cap_frame_cap_get_capFMappedAddress(cap);
        frameSize   = cap_frame_cap_get_capFSize(cap);
        capVMRights = cap_frame_cap_get_capFVMRights(cap);
        paddr       = pptr_to_paddr((void*)cap_frame_cap_get_capFBasePtr(cap));

        vmRights = maskVMRights(capVMRights, rightsFromWord(w_rightsMask));

        switch (frameSize) {
        /* PTE mappings */
        case IA32_SmallPage: {
            pte_t              pte;
            lookupPTSlot_ret_t lu_ret;

            lu_ret = lookupPTSlot(vspace, vaddr);
            if (lu_ret.status != EXCEPTION_NONE) {
                current_syscall_error.type = seL4_FailedLookup;
                current_syscall_error.failedLookupWasSource = false;
                /* current_lookup_fault will have been set by lookupPTSlot */
                return EXCEPTION_SYSCALL_ERROR;
            }

            pte = makeUserPTE(paddr, vmAttr, vmRights);
            *lu_ret.ptSlot = pte;

            break;
        }

        /* PDE mappings */
        case IA32_LargePage: {
            pde_t* pdeSlot;
            lookupPDSlot_ret_t lu_ret;

            lu_ret = lookupPDSlot(vspace, vaddr);
            if (lu_ret.status != EXCEPTION_NONE) {
                current_syscall_error.type = seL4_FailedLookup;
                current_syscall_error.failedLookupWasSource = false;
                /* current_lookup_fault will have been set by lookupPDSlot */
                return EXCEPTION_SYSCALL_ERROR;
            }
            pdeSlot = lu_ret.pdSlot;

            if ((pde_ptr_get_page_size(pdeSlot) == pde_pde_small) &&
                    (pde_pde_small_ptr_get_present(pdeSlot))) {
                current_syscall_error.type = seL4_DeleteFirst;

                return EXCEPTION_SYSCALL_ERROR;
            }

            *pdeSlot = makeUserPDE(paddr, vmAttr, vmRights);

            break;
        }

        default:
            fail("Invalid page type");
        }
        invalidatePageStructureCache();
        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

    case IA32PageUnmap: { /* Unmap */
        if (cap_frame_cap_get_capFMappedASID(cap) != asidInvalid) {
#ifdef CONFIG_IOMMU
            if (cap_frame_cap_get_capFIsIOSpace(cap)) {
                return decodeIA32IOUnMapInvocation(label, length, cte, cap, extraCaps);
            }
#endif
            unmapPage(
                cap_frame_cap_get_capFSize(cap),
                cap_frame_cap_get_capFMappedASID(cap),
                cap_frame_cap_get_capFMappedAddress(cap),
                (void *)cap_frame_cap_get_capFBasePtr(cap)
            );
        }
        cap_frame_cap_ptr_set_capFMappedAddress(&cte->cap, 0);
        cap_frame_cap_ptr_set_capFMappedASID(&cte->cap, asidInvalid);

        setThreadState(ksCurThread, ThreadState_Restart);
        return EXCEPTION_NONE;
    }

#ifdef CONFIG_IOMMU
    case IA32PageMapIO: { /* MapIO */
        return decodeIA32IOMapInvocation(label, length, cte, cap, extraCaps, buffer);
    }
#endif

    case IA32PageGetAddress: {
        /* Return it in the first message register. */
        assert(n_msgRegisters >= 1);

        setThreadState(ksCurThread, ThreadState_Restart);
        return performPageGetAddress((void*)cap_frame_cap_get_capFBasePtr(cap));
    }

    default:
        current_syscall_error.type = seL4_IllegalOperation;

        return EXCEPTION_SYSCALL_ERROR;
    }
}

exception_t
decodeIA32MMUInvocation(
    word_t label,
    unsigned int length,
    cptr_t cptr,
    cte_t* cte,
    cap_t cap,
    extra_caps_t extraCaps,
    word_t* buffer
)
{
    switch (cap_get_capType(cap)) {
    case cap_pdpt_cap:
        current_syscall_error.type = seL4_IllegalOperation;
        return EXCEPTION_SYSCALL_ERROR;

    case cap_page_directory_cap:
        return decodeIA32PageDirectoryInvocation(label, length, cte, cap, extraCaps, buffer);

    case cap_page_table_cap:
        return decodeIA32PageTableInvocation(label, length, cte, cap, extraCaps, buffer);

    case cap_frame_cap:
        return decodeIA32FrameInvocation(label, length, cte, cap, extraCaps, buffer);

    case cap_asid_control_cap: {
        unsigned int     i;
        asid_t           asid_base;
        word_t           index;
        word_t           depth;
        cap_t            untyped;
        cap_t            root;
        cte_t*           parentSlot;
        cte_t*           destSlot;
        lookupSlot_ret_t lu_ret;
        void*            frame;
        exception_t      status;

        if (label != IA32ASIDControlMakePool) {
            current_syscall_error.type = seL4_IllegalOperation;

            return EXCEPTION_SYSCALL_ERROR;
        }

        if (length < 2 || extraCaps.excaprefs[0] == NULL
                || extraCaps.excaprefs[1] == NULL) {
            current_syscall_error.type = seL4_TruncatedMessage;

            return EXCEPTION_SYSCALL_ERROR;
        }

        index = getSyscallArg(0, buffer);
        depth = getSyscallArg(1, buffer);
        parentSlot = extraCaps.excaprefs[0];
        untyped = parentSlot->cap;
        root = extraCaps.excaprefs[1]->cap;

        /* Find first free pool */
        for (i = 0; i < nASIDPools && ia32KSASIDTable[i]; i++);

        if (i == nASIDPools) {
            /* no unallocated pool is found */
            current_syscall_error.type = seL4_DeleteFirst;

            return EXCEPTION_SYSCALL_ERROR;
        }

        asid_base = i << asidLowBits;

        if (cap_get_capType(untyped) != cap_untyped_cap ||
                cap_untyped_cap_get_capBlockSize(untyped) != ASID_POOL_SIZE_BITS) {
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 1;

            return EXCEPTION_SYSCALL_ERROR;
        }

        status = ensureNoChildren(parentSlot);
        if (status != EXCEPTION_NONE) {
            return status;
        }

        frame = WORD_PTR(cap_untyped_cap_get_capPtr(untyped));

        lu_ret = lookupTargetSlot(root, index, depth);
        if (lu_ret.status != EXCEPTION_NONE) {
            return lu_ret.status;
        }
        destSlot = lu_ret.slot;

        status = ensureEmptySlot(destSlot);
        if (status != EXCEPTION_NONE) {
            return status;
        }

        setThreadState(ksCurThread, ThreadState_Restart);
        return performASIDControlInvocation(frame, destSlot, parentSlot, asid_base);
    }

    case cap_asid_pool_cap: {
        cap_t        vspaceCap;
        cte_t*       vspaceCapSlot;
        asid_pool_t* pool;
        unsigned int i;
        asid_t       asid;

        if (label != IA32ASIDPoolAssign) {
            current_syscall_error.type = seL4_IllegalOperation;

            return EXCEPTION_SYSCALL_ERROR;
        }

        if (extraCaps.excaprefs[0] == NULL) {
            current_syscall_error.type = seL4_TruncatedMessage;

            return EXCEPTION_SYSCALL_ERROR;
        }

        vspaceCapSlot = extraCaps.excaprefs[0];
        vspaceCap = vspaceCapSlot->cap;

        if (!isVTableRoot(vspaceCap) ||
                cap_get_capMappedASID(vspaceCap) != asidInvalid) {
            userError("IA32ASIDPool: Invalid vspace root.");
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 1;

            return EXCEPTION_SYSCALL_ERROR;
        }

        pool = ia32KSASIDTable[cap_asid_pool_cap_get_capASIDBase(cap) >> asidLowBits];
        if (!pool) {
            current_syscall_error.type = seL4_FailedLookup;
            current_syscall_error.failedLookupWasSource = false;
            current_lookup_fault = lookup_fault_invalid_root_new();
            return EXCEPTION_SYSCALL_ERROR;
        }

        if (pool != ASID_POOL_PTR(cap_asid_pool_cap_get_capASIDPool(cap))) {
            current_syscall_error.type = seL4_InvalidCapability;
            current_syscall_error.invalidCapNumber = 0;
            return EXCEPTION_SYSCALL_ERROR;
        }

        /* Find first free ASID */
        asid = cap_asid_pool_cap_get_capASIDBase(cap);
        for (i = 0; i < BIT(asidLowBits) && (asid + i == 0 || pool->array[i]); i++);

        if (i == BIT(asidLowBits)) {
            current_syscall_error.type = seL4_DeleteFirst;

            return EXCEPTION_SYSCALL_ERROR;
        }

        asid += i;

        setThreadState(ksCurThread, ThreadState_Restart);
        return performASIDPoolInvocation(asid, pool, vspaceCapSlot);
    }

    default:
        fail("Invalid arch cap type");
    }
}
