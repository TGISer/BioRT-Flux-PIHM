#include "pihm.h"

/* Global variables */
int             verbose_mode;
int             debug_mode;
int             corr_mode;
int             spinup_mode;
int             tecplot;
char            project[MAXSTRING];
int             nelem;
int             nriver;
#if defined(_OPENMP)
int             nthreads = 1;    /* Default value */
#endif
#if defined(_BGC_) || defined (_CYCLES_)
int             first_balance;
#endif

int main(int argc, char *argv[])
{
    char            outputdir[MAXSTRING];
    pihm_struct     pihm;
    N_Vector        CV_Y;
    void           *cvode_mem;
    int             i;
    Chem_Data       chData;     // 12.30 RT use
    time_t          t_start, t_end, t_start_hydro, t_end_hydro, t_start_rt, t_end_rt;  // 12.30 timing
    double          t_duration, t_duration_hydro = 0.0, t_duration_rt = 0.0;           // 12.30 timing
    double          t_zero1 = 0.0, t_zero2 = 0.0;                                      // 12.30 timing
    double          *t_duration_transp = &t_zero1, *t_duration_react = &t_zero2;       // 12.30 timing
    
    // 12.30
    t_start = time(NULL);  // 12.30 timing
    
#if defined(_OPENMP)
    double          start_omp;
#else
    clock_t         start;
#endif
    double          cputime, cputime_dt;    /* Time cpu duration */

#if defined(_OPENMP)
    /* Set the number of threads to use */
    nthreads = omp_get_max_threads();
#endif

    memset(outputdir, 0, MAXSTRING);

    /* Print AscII art */
    AsciiArt();

    /* Read command line arguments */
    ParseCmdLineParam(argc, argv, outputdir);

    /* Allocate memory for model data structure */
    pihm = (pihm_struct)malloc(sizeof(*pihm));
    chData = (Chem_Data)malloc(sizeof (*chData));     // 12.30 RT use

    /* Read PIHM input files */
    ReadAlloc(pihm);

    /* Initialize CVode state variables */
    CV_Y = N_VNew(NumStateVar());
    if (CV_Y == NULL)
    {
        PIHMprintf(VL_ERROR, "Error creating CVODE state variable vector.\n");
        PIHMexit(EXIT_FAILURE);
    }

    /* Initialize PIHM structure */
    Initialize(pihm, CV_Y, &cvode_mem);
    
    // 12.30 RT use, must be placed after Initialize()
    chem_alloc(project, pihm, CV_Y, chData, (double)(pihm->ctrl.starttime/60));  // 12.30 RT use

    /* Create output directory */
    CreateOutputDir(outputdir);

    /* Create output structures */
    MapOutput(pihm->ctrl.prtvrbl, pihm->ctrl.tpprtvrbl, pihm->elem, pihm->river,
        &pihm->meshtbl, outputdir, &pihm->print);
        
    // 12.30 RT use, must be placed after MapOutput()    
    InitialChemFile(outputdir, project, chData->NumBTC, chData->BTC_loc);  // 12.30 RT use    

    /* Backup input files */
#if !defined(_MSC_VER)
    BackupInput(outputdir);
#endif

    InitOutputFile(&pihm->print, outputdir, pihm->ctrl.waterbal,
        pihm->ctrl.ascii);

    PIHMprintf(VL_VERBOSE, "\n\nSolving ODE system ... \n\n");

    /* Set solver parameters */
    SetCVodeParam(pihm, cvode_mem, CV_Y);

#if defined(_BGC_) || defined (_CYCLES_)
    first_balance = 1;
#endif

    /*
     * Run PIHM
     */
#if defined(_OPENMP)
    start_omp = omp_get_wtime();
#else
    start = clock();
#endif

#if defined(_BGC_)
    if (spinup_mode)
    {
        BgcSpinup(pihm, CV_Y, cvode_mem);
    }
    else
    {
#endif
        for (i = 0; i < pihm->ctrl.nstep; i++)
        {
#if defined(_OPENMP)
            RunTime(start_omp, &cputime, &cputime_dt);
#else
            RunTime(start, &cputime, &cputime_dt);
#endif

            t_start_hydro = time(NULL);                        // 12.30 timing hydro
            PIHM(pihm, cvode_mem, CV_Y, pihm->ctrl.tout[i],
                pihm->ctrl.tout[i + 1], cputime);
            t_end_hydro = time(NULL);                          // 12.30 timing hydro
            t_duration_hydro += t_end_hydro - t_start_hydro;   // 12.30 timing hydro

            /* Adjust CVODE max step to reduce oscillation */
            AdjCVodeMaxStep(cvode_mem, &pihm->ctrl);

            /* Print CVODE performance and statistics */
            if (debug_mode)
            {
                PrintPerf(cvode_mem, pihm->ctrl.tout[i + 1],
                    pihm->ctrl.starttime, cputime_dt, cputime,
                    pihm->ctrl.maxstep, pihm->print.cvodeperf_file);
            }

            /* Write init files */
            if (pihm->ctrl.write_ic)
            {
                PrintInit(pihm->elem, pihm->river, outputdir,
                    pihm->ctrl.tout[i + 1], pihm->ctrl.starttime,
                    pihm->ctrl.endtime, pihm->ctrl.prtvrbl[IC_CTRL]);
            }
            
            // 12.30 RT control
            t_start_rt = time(NULL);
            fluxtrans(pihm->ctrl.tout[i + 1]/60, pihm->ctrl.stepsize/60, pihm, chData, CV_Y, t_duration_transp, t_duration_react);  // 12.30 add two timers
            t_end_rt = time(NULL);
            t_duration_rt += t_end_rt - t_start_rt;
            
            PrintChem(outputdir, project, chData, pihm->ctrl.tout[i + 1]/60);  // 12.30 RT use
        }
#if defined(_BGC_)
    }
#endif

#if defined(_BGC_)
    if (pihm->ctrl.write_bgc_restart)
    {
        WriteBgcIc(outputdir, pihm->elem, pihm->river);

        if (spinup_mode)
        {
            /* In spin-up mode, also print land surface and hydrologic initial
             * conditions */
            PrintInit(pihm->elem, pihm->river, outputdir,
                pihm->ctrl.endtime, pihm->ctrl.starttime,
                pihm->ctrl.endtime, pihm->ctrl.prtvrbl[IC_CTRL]);
        }
    }
#endif

#if defined(_CYCLES_)
    if (pihm->ctrl.write_cycles_restart)
    {
        WriteCyclesIC(pihm->filename.cyclesic, pihm->elem, pihm->river);
    }
#endif

    if (debug_mode)
    {
        PrintCVodeFinalStats(cvode_mem);
    }

    /* Free memory */
    N_VDestroy(CV_Y);

    /* Free integrator memory */
    CVodeFree(&cvode_mem);
    FreeData(pihm);
    free(pihm);
    
    // 12.30, RT use
    FreeChem(chData);  // 12.30, RT use
    free(chData);
    
    PIHMprintf(VL_NORMAL, "\nSimulation completed.\n");
    
    // 12.30, RT use
    t_end = time(NULL);
    t_duration = t_end - t_start; 
    fprintf(stderr, "\n# of threads = %d. \n", nthreads);
    fprintf(stderr, "Wall time of simulation = %.3f [min] or %.3f [hr]. \n", t_duration/60, t_duration/3600);
    fprintf(stderr, "Wall time of hydro step = %.3f [min] or %.3f [hr]. \n", t_duration_hydro/60, t_duration_hydro/3600);
    fprintf(stderr, "Wall time of rt step = %.3f [min] or %.3f [hr]. \n", t_duration_rt/60, t_duration_rt/3600);
    fprintf(stderr, "Wall time of rt_transp = %.3f [min] or %.3f [hr]. \n", *t_duration_transp/60, *t_duration_transp/3600);
    fprintf(stderr, "Wall time of rt_react = %.3f [min] or %.3f [hr]. \n", *t_duration_react/60, *t_duration_react/3600);

    return EXIT_SUCCESS;
}
