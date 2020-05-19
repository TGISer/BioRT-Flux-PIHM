/******************************************************************************
* This subroutine is used to calculate the reactions
* It uses a similar OS3D scheme as detailed inCrunchflow user's manual.
*****************************************************************************/

#include "pihm.h" 
#define UNIT_C     1440
#define ZERO       1E-20
#define LINE_WIDTH 512
#define WORDS_LINE 40
#define WORD_WIDTH 80
#define EPSILON    1E-3
#define TOL        1E-7
#define TIGHT      10
#define SKIP_JACOB 1
#define sqr(a)  (a)*(a)

int  keymatch(char *, char *, double *, char **);
int  React(realtype, realtype, Chem_Data, int, int *, const pihm_struct);  // 10.01
void wrap(char* str) 
{
	char word[WORD_WIDTH];
	sprintf(word, "'%s'", str);
	strcpy(str, word);
}

void ReportError(vol_conc cell, Chem_Data  CD) {

	// This subroutine checks all the important parameters within a cell and output it to the error log
	// Diagnotic purposes
	int i;
	fprintf(stderr, "Error found at cell %d\n", cell.index);
	fprintf(stderr, "Volumetric properties:\n\t sat: %8.6f\t, sat_o: %8.6f\t, height_o: %8.6f\t, height_t: %8.6f\t, height_v: %8.6f\t, area: %8.6f, Error: %d\n", cell.sat, cell.sat_o, cell.height_o, cell.height_t, cell.height_v, cell.area, cell.illness);
	fprintf(stderr, "Chemical total concentrations: Chemical  Tot_conc  Log(Tot_conc)\n");
	for (i = 0; i < CD->NumStc; i++)
	{
		fprintf(stderr, "\t%20s\t%8.4f\t%8.4f\t\n", CD->chemtype[i].ChemName, cell.t_conc[i], log10(cell.t_conc[i]));
	}
	fprintf(stderr, "\n");
}


int SpeciationType(FILE* database, char * Name) {
	/* This subroutine is used to find out what the input species is.
	0) not found within database
	1) aqueous
	2) adsorption
	3) cation exchange
	4) mineral
	*/
	double tmpval[WORDS_LINE];
	int i, j, return_val;
	char keyword[WORD_WIDTH], line[LINE_WIDTH], word[WORD_WIDTH];

	if (strcmp(Name, "pH") == 0) return(1);

	char ** tmpstr = (char**)malloc(WORDS_LINE * sizeof(char*));
	for (i = 0; i < WORDS_LINE; i++)
		tmpstr[i] = (char*)malloc(WORD_WIDTH * sizeof(char));

	return_val = 0;

	sprintf(word, "'%s'", Name);
	rewind(database);
	fgets(line, LINE_WIDTH, database);
	while (keymatch(line, "'End of primary'", tmpval, tmpstr) != 1) {
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2) {
			if (strcmp(word, tmpstr[0]) == 0) {
				for (i = 0; i < WORDS_LINE; i++)
					free(tmpstr[i]);
				free(tmpstr);
				return(1);
			}
		}
		fgets(line, LINE_WIDTH, database);
	}
	fgets(line, LINE_WIDTH, database);
	while (keymatch(line, "'End of secondary'", tmpval, tmpstr) != 1) {
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2) {
			if (strcmp(word, tmpstr[0]) == 0) {
				for (i = 0; i < WORDS_LINE; i++)
					free(tmpstr[i]);
				free(tmpstr);
				return(5);
			}
		}
		fgets(line, LINE_WIDTH, database);
	}
	fgets(line, LINE_WIDTH, database);
	while (keymatch(line, "'End of minerals'", tmpval, tmpstr) != 1) {
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2) {
			if (strcmp(word, tmpstr[0]) == 0) {
				for (i = 0; i < WORDS_LINE; i++)
					free(tmpstr[i]);
				free(tmpstr);
				return(4);
			}
		}
		fgets(line, LINE_WIDTH, database);
	}
	fgets(line, LINE_WIDTH, database);
	while (strcmp(line, "End of surface complexation\r\n") != 1) {
		// notice that in crunchflow database, starting from surface complexation, there is not apostrophe marks around blocking keywords.
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2) {
			if (strcmp(word, tmpstr[0]) == 0) {
				for (i = 0; i < WORDS_LINE; i++)
					free(tmpstr[i]);
				free(tmpstr);
				return(2);
			}
		}
		fgets(line, LINE_WIDTH, database);
	}
	fgets(line, LINE_WIDTH, database);
	while (!feof(database)) {
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2) {
			if (strcmp(word, tmpstr[0]) == 0) {
				for (i = 0; i < WORDS_LINE; i++)
					free(tmpstr[i]);
				free(tmpstr);
				return(3);
			}
		}
		fgets(line, LINE_WIDTH, database);
	}

	for (i = 0; i < WORDS_LINE; i++)
		free(tmpstr[i]);
	free(tmpstr);

	return(0);
}


void Lookup(FILE* database, Chem_Data CD, int lookupflg) {

	/* lookupflg: used to lookup for
	0) uniform temperature field ( thermo decoupled ). Read temperature from CD->Temperature and return value for the whole field.
	1) values for each grid block at each time ( thermo coupled ). Read temperature from each grid block and return value for each grid block;
	*/
	double tmpval[WORDS_LINE];
	//  CD->Keq = (double*) malloc( CD->NumSsc * sizeof(double));
	//  CD->KeqKinect = (double*) malloc( CD->NumMin * sizeof(double));

	// Kinetic reactions is currently only applicable to minerals
	int i, j, k, l, keq_position, total_temp_points;
	int mn, in;      // 08.19 Wei
	char keyword[WORD_WIDTH], line[LINE_WIDTH], word[WORD_WIDTH], tmp[WORD_WIDTH];
	char ** tmpstr = (char**)malloc(WORDS_LINE * sizeof(char*));


	for (i = 0; i < WORDS_LINE; i++)
	{
		tmpstr[i] = (char*)malloc(WORD_WIDTH * sizeof(char));
		memset(tmpstr[i], 0, WORD_WIDTH);
	}

	for (i = 0; i < CD->NumStc + CD->NumSsc; i++)
		wrap(CD->chemtype[i].ChemName);
	rewind(database);
	fgets(line, LINE_WIDTH, database);
	while (keymatch(line, "'temperature points'", tmpval, tmpstr) != 1) {
		fgets(line, LINE_WIDTH, database);
	}
	total_temp_points = tmpval[0];
	for (i = 0; i < tmpval[0]; i++) {
		if (tmpval[i + 1] == CD->Temperature) {
			fprintf(stderr, "\n Temperature point %6.4f C found in database!\n\n", tmpval[i + 1]);
			keq_position = i + 1;
		}
	}
	while (keymatch(line, "'Debye-Huckel adh'", tmpval, tmpstr) != 1) {
		fgets(line, LINE_WIDTH, database);
	}
	CD->DH.adh = tmpval[keq_position - 1];
	while (keymatch(line, "'Debye-Huckel bdh'", tmpval, tmpstr) != 1) {
		fgets(line, LINE_WIDTH, database);
	}
	CD->DH.bdh = tmpval[keq_position - 1];
	while (keymatch(line, "'Debye-Huckel bdt'", tmpval, tmpstr) != 1) {
		fgets(line, LINE_WIDTH, database);
	}
	CD->DH.bdt = tmpval[keq_position - 1];

	fprintf(stderr, " Debye-Huckel Parameters set to A=%6.4f; B=%6.4f; b=%6.4f\n\n", CD->DH.adh, CD->DH.bdh, CD->DH.bdt);

	rewind(database);
	fgets(line, LINE_WIDTH, database);
	while (keymatch(line, "'End of primary'", tmpval, tmpstr) != 1)
	{
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2)
		{
			for (i = 0; i < CD->NumStc; i++)
				if (strcmp(CD->chemtype[i].ChemName, tmpstr[0]) == 0)
				{
					fprintf(stderr, " Primary species %s found in database!\n MolarMass = %6.4f\n\n", CD->chemtype[i].ChemName, tmpval[2]);
					CD->chemtype[i].MolarMass = tmpval[2];
					CD->chemtype[i].Charge = tmpval[1];
					CD->chemtype[i].SizeF = tmpval[0];
				}
		}
		fgets(line, LINE_WIDTH, database);
	}
	while (keymatch(line, "'End of secondary'", tmpval, tmpstr) != 1)
	{
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2)
		{
			for (i = CD->NumStc; i < CD->NumSsc + CD->NumStc; i++)
				if (strcmp(CD->chemtype[i].ChemName, tmpstr[0]) == 0)
				{
					fprintf(stderr, " Secondary species %s found in database!\n", CD->chemtype[i].ChemName);
					fprintf(stderr, " %s", line);
					CD->chemtype[i].itype = 1;
					for (j = 0; j < WORDS_LINE; j++)
					{
						//  fprintf(stderr, "%6.4f\t%s\n",tmpval[j],tmpstr[j]);
						for (k = 0; k < CD->NumSdc; k++)
							if (strcmp(CD->chemtype[k].ChemName, tmpstr[j]) == 0)
								CD->Dependency[i - CD->NumStc][k] = atof(tmpstr[j - 1]);
					}
					CD->Keq[i - CD->NumStc] = tmpval[(int)tmpval[0] + keq_position];
					fprintf(stderr, " Keq = %6.4f\n", CD->Keq[i - CD->NumStc]);
					CD->chemtype[i].MolarMass = tmpval[(int)tmpval[0] + total_temp_points + 3];
					CD->chemtype[i].Charge = tmpval[(int)tmpval[0] + total_temp_points + 2];
					CD->chemtype[i].SizeF = tmpval[(int)tmpval[0] + total_temp_points + 1];
					fprintf(stderr, " MolarMass = %6.4f, Charge = %6.4f, SizeFactor = %6.4f\n\n", CD->chemtype[i].MolarMass, CD->chemtype[i].Charge, CD->chemtype[i].SizeF);
				}
		}
		fgets(line, LINE_WIDTH, database);
	}
	while (keymatch(line, "'End of minerals'", tmpval, tmpstr) != 1)
	{
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2)
		{
			for (i = CD->NumSpc + CD->NumAds + CD->NumCex; i < CD->NumStc; i++)
				if (strcmp(CD->chemtype[i].ChemName, tmpstr[0]) == 0)
				{
					fprintf(stderr, " Mineral %s found in database!\n", CD->chemtype[i].ChemName);
					fprintf(stderr, " %s", line);
					CD->chemtype[i].itype = 4;
					CD->KeqKinect_all[i - CD->NumSpc - CD->NumAds - CD->NumCex] = tmpval[(int)tmpval[1] + keq_position + 1];
					for (j = 1; j < WORDS_LINE; j++)
					{
						//  fprintf(stderr, "%6.4f\t%s\n",tmpval[j],tmpstr[j]);
						for (k = 0; k < CD->NumStc + CD->NumSsc; k++)
							if (strcmp(CD->chemtype[k].ChemName, tmpstr[j]) == 0)
							{
								if (k < CD->NumStc)
								{
									CD->Dep_kinetic_all[i - CD->NumStc + CD->NumMin][k] = atof(tmpstr[j - 1]);
								}
								else {
									for (l = 0; l < CD->NumSpc; l++)  /* Number of primary species in the rt simulator      */
										CD->Dep_kinetic_all[i - CD->NumStc + CD->NumMin][l] += atof(tmpstr[j - 1])* CD->Dependency[k - CD->NumStc][l];
									CD->KeqKinect_all[i - CD->NumSpc - CD->NumAds - CD->NumCex] += atof(tmpstr[j - 1]) * CD->Keq[k - CD->NumStc];
								}
							}
					}
					CD->Dep_kinetic_all[i - CD->NumStc + CD->NumMin][i] = -1.0;
					fprintf(stderr, " Keq = %6.4f\n", CD->KeqKinect_all[i - CD->NumSpc - CD->NumAds - CD->NumCex]);
					CD->chemtype[i].MolarMass = tmpval[(int)tmpval[1] + total_temp_points + 2];
					CD->chemtype[i].MolarVolume = tmpval[0];
					CD->chemtype[i].Charge = 0;
					fprintf(stderr, " MolarMass = %6.4f, MolarVolume = %6.4f\n\n", CD->chemtype[i].MolarMass, CD->chemtype[i].MolarVolume);
				}
		}
		fgets(line, LINE_WIDTH, database);
	}

	for (i = 0; i < CD->NumMkr + CD->NumAkr; i++)
	{
		for (j = CD->NumSpc + CD->NumAds + CD->NumCex; j < CD->NumStc; j++)
		{
			strcpy(tmp, CD->kinetics[i].species);
			wrap(tmp);
			if (strcmp(tmp, CD->chemtype[j].ChemName) == 0)
			{
				fprintf(stderr, " Selecting the kinetic species %s from all possible species.\n\n", tmp);
				CD->KeqKinect[i] = CD->KeqKinect_all[j - CD->NumStc + CD->NumMin];
				//	fprintf(stderr, " %6.4f\n", CD->KeqKinect[j - CD->NumStc + CD->NumMin]);
				for (k = 0; k < CD->NumStc; k++)
				{
					CD->Dep_kinetic[i][k] = CD->Dep_kinetic_all[j - CD->NumStc + CD->NumMin][k];
				}
			}
		}
	}
	while (strcmp(line, "End of surface complexation\r\n") != 1) {
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2) {
			for (i = CD->NumStc; i < CD->NumSsc + CD->NumStc; i++)
				if (strcmp(CD->chemtype[i].ChemName, tmpstr[0]) == 0) {
					fprintf(stderr, " Secondary surface complexation %s found in database!\n", CD->chemtype[i].ChemName);
					fprintf(stderr, " %s", line);
					CD->chemtype[i].itype = 2;
					for (j = 0; j < WORDS_LINE; j++) {
						//      fprintf(stderr, "%6.4f\t%s\n",tmpval[j],tmpstr[j]);
						for (k = 0; k < CD->NumSdc; k++)
							if (strcmp(CD->chemtype[k].ChemName, tmpstr[j]) == 0)
								CD->Dependency[i - CD->NumStc][k] = atof(tmpstr[j - 1]);
					}
					CD->Keq[i - CD->NumStc] = tmpval[(int)tmpval[0] + keq_position];
					fprintf(stderr, " Keq = %6.4f\n", CD->Keq[i - CD->NumStc]);
				}
		}
		fgets(line, LINE_WIDTH, database);
	}
	while (!feof(database)) {
		if (keymatch(line, "NULL", tmpval, tmpstr) != 2) {
			for (i = CD->NumStc; i < CD->NumSsc + CD->NumStc; i++)
				if (strcmp(CD->chemtype[i].ChemName, tmpstr[0]) == 0) {
					fprintf(stderr, " Secondary ion exchange %s found in database!\n", CD->chemtype[i].ChemName);
					fprintf(stderr, " %s", line);
					CD->chemtype[i].itype = 3;
					for (j = 0; j < WORDS_LINE; j++) {
						//    fprintf(stderr, "%6.4f\t%s\n",tmpval[j],tmpstr[j]);
						for (k = 0; k < CD->NumSdc; k++)
							if (strcmp(CD->chemtype[k].ChemName, tmpstr[j]) == 0)
								CD->Dependency[i - CD->NumStc][k] = atof(tmpstr[j - 1]);
					}
					CD->Keq[i - CD->NumStc] = tmpval[(int)tmpval[0] + 1];
					fprintf(stderr, " Keq = %6.4f \n", CD->Keq[i - CD->NumStc]);
            
          CD->Keq[i - CD->NumStc] = tmpval[(int)tmpval[0] + 1] + CD->CalXsorption; 
          fprintf(stderr, " After calibration: Keq = %6.4f \n", CD->Keq[i - CD->NumStc]);                                      
				}
		}
		fgets(line, LINE_WIDTH, database);
	}
	/*
	while (strcmp(line, "End of surface complexation parameters\r\n")!=1){
	if (keymatch(line, "NULL", tmpval,tmpstr)!=2){
	for( i = 0 ; i < CD->NumSsc + CD->NumStc; i ++)
	if(strcmp(CD->chemtype[i].ChemName, tmpstr[0])==0){
	fprintf(stderr, " Property of surface complexation %s found in database!\n",CD->chemtype[i].ChemName);
	CD->chemtype[i].Charge   = tmpval[0];;
	fprintf(stderr, " Charge = %6.4f\n", CD->chemtype[i].Charge);
	}

	}
	fgets(line, LINE_WIDTH, database);
	}
	*/

	/*  for ( i = 0; i < CD->NumMkr; i ++){
	fprintf(stderr, " K %s L %s\n", CD->kinetics[i].species, CD->kinetics[i].Label);
	}
	*/
	for (i = 0; i < CD->NumMkr; i++)
	{
		rewind(database);
		fgets(line, LINE_WIDTH, database);
		while (strcmp(line, "Begin mineral kinetics\r\n") != 0)
		{
			fgets(line, LINE_WIDTH, database);
		}
		fgets(line, LINE_WIDTH, database);
		while (strcmp(line, "End of mineral kinetics\r\n") != 0)
		{
			if (keymatch(line, "NULL", tmpval, tmpstr) != 2)
			{
				if (strcmp(CD->kinetics[i].species, tmpstr[0]) == 0)
				{
					//  fprintf(stderr, " XXXX %s found in kinetics\n", CD->kinetics[i].species);
					fgets(line, LINE_WIDTH, database);
					keymatch(line, "NULL", tmpval, tmpstr);
					//	fprintf(stderr, " Labels %s ", tmpstr[2]); for (j = 0; j < strlen(tmpstr[2]); j++) fprintf(stderr, "%d\t",tmpstr[2][j]); fprintf(stderr, "\n");
					if (strcmp(CD->kinetics[i].Label, tmpstr[2]) == 0)
					{
						fprintf(stderr, " \n Mineral kinetics %s %s found in database!\n", CD->kinetics[i].species, CD->kinetics[i].Label);
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[2], "tst") == 0) CD->kinetics[i].type = 1;
						if (strcmp(tmpstr[2], "PrecipitationOnly") == 0) CD->kinetics[i].type = 2;
						if (strcmp(tmpstr[2], "DissolutionOnly") == 0) CD->kinetics[i].type = 3;
						if (strcmp(tmpstr[2], "monod") == 0) CD->kinetics[i].type = 4;
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[0], "rate(25C)") == 0)
						{
							CD->kinetics[i].rate = tmpval[0];
							fprintf(stderr, " Rate is %f\n", CD->kinetics[i].rate);
                                       
              CD->kinetics[i].rate = tmpval[0] + CD->CalRate; 
              fprintf(stderr, " After calibration: Rate is %f, CD->CalRate = %f \n", CD->kinetics[i].rate, CD->CalRate);                       
						}
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[0], "activation") == 0)
						{
							CD->kinetics[i].actv = tmpval[0];
							fprintf(stderr, " Activation is %f\n", CD->kinetics[i].actv);
						}
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[0], "dependence") == 0)
						{
							strcpy(CD->kinetics[i].dep_species[0], tmpstr[2]);
							wrap(CD->kinetics[i].dep_species[0]);
							CD->kinetics[i].num_dep = 1;
							// assume that all mineral kinetic only depend on one species !!

							/* require further elaboration after this !! */

							for (k = 0; k < CD->NumStc; k++)
							{
								if (strcmp(CD->kinetics[i].dep_species[0], CD->chemtype[k].ChemName) == 0)
									CD->kinetics[i].dep_position[0] = k;
							}
							CD->kinetics[i].dep_power[0] = tmpval[0];
							fprintf(stderr, " Dependency: %s %f\n", CD->kinetics[i].dep_species[0], CD->kinetics[i].dep_power[0]);
						}

						// Biomass term
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[0], "biomass") == 0)
						{
							strcpy(CD->kinetics[i].biomass_species, tmpstr[2]);
							wrap(CD->kinetics[i].biomass_species);
							fprintf(stderr, " Biomass species: %s \n", CD->kinetics[i].biomass_species);
							for (k = 0; k < CD->NumStc; k++)
							{
								if (strcmp(CD->kinetics[i].biomass_species, CD->chemtype[k].ChemName) == 0)
								{
									CD->kinetics[i].biomass_position = k;
									fprintf(stderr, " Biomass species position: %d \n", CD->kinetics[i].biomass_position);
								}
							}
						}

						// Monod term
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[0], "num_monod") == 0)
						{
							CD->kinetics[i].num_monod = tmpval[0];
							fprintf(stderr, " Number of monod term: %d\n", CD->kinetics[i].num_monod);
						}
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[0], "monod_terms") == 0)
						{
							for (mn = 0; mn < CD->kinetics[i].num_monod; mn++)
							{
								strcpy(CD->kinetics[i].monod_species[mn], tmpstr[mn * 2 + 2]); 
								wrap(CD->kinetics[i].monod_species[mn]);
								for (k = 0; k < CD->NumStc; k++)
								{
									if (strcmp(CD->kinetics[i].monod_species[mn], CD->chemtype[k].ChemName) == 0)
										CD->kinetics[i].monod_position[mn] = k;
								}
								CD->kinetics[i].monod_para[mn] = tmpval[mn + 0];
								fprintf(stderr, " Monod term: %s %f\n", CD->kinetics[i].monod_species[mn], CD->kinetics[i].monod_para[mn]);
							}
						}

						// Inhibition term
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[0], "num_inhibition") == 0)
						{
							CD->kinetics[i].num_inhib = tmpval[0];
							fprintf(stderr, " Number of inhibition term: %d\n", CD->kinetics[i].num_inhib);
						}
						fgets(line, LINE_WIDTH, database);
						keymatch(line, "NULL", tmpval, tmpstr);
						if (strcmp(tmpstr[0], "inhibition") == 0)
						{
							for (in = 0; in < CD->kinetics[i].num_inhib; in++)
							{
								strcpy(CD->kinetics[i].inhib_species[in], tmpstr[in * 2 + 2]); // 08.19 pay attention, tmpstr indexing not same with tmpval indexing
								wrap(CD->kinetics[i].inhib_species[in]);
								for (k = 0; k < CD->NumStc; k++)
								{
									if (strcmp(CD->kinetics[i].inhib_species[in], CD->chemtype[k].ChemName) == 0)
										CD->kinetics[i].inhib_position[in] = k;
								}
								CD->kinetics[i].inhib_para[in] = tmpval[in + 0];
								fprintf(stderr, " Inhibition term: %s %f\n", CD->kinetics[i].inhib_species[in], CD->kinetics[i].inhib_para[in]);
							}
						}
						wrap(CD->kinetics[i].species);
					}
				}
			}
			fgets(line, LINE_WIDTH, database);
		}
	}

	for (i = 0; i < CD->NumMkr; i++)
		for (j = 0; j < CD->NumStc; j++)
		{
			if (strcmp(CD->kinetics[i].species, CD->chemtype[j].ChemName) == 0)
			{
				CD->kinetics[i].position = j;
				// NumMkr[i] vs NumSt[j]
				// fprintf(stderr, " \n Position_check (NumMkr[i] vs NumStc[j]) %s (%d), %s (%d)\n", CD->kinetics[i].species, i, CD->chemtype[j].ChemName, j);
			}
		}

	for (i = 0; i < CD->NumStc; i++)
		CD->Totalconc[i][i] = 1.0;

	for (i = CD->NumStc; i < CD->NumStc + CD->NumSsc; i++)
		for (j = 0; j < CD->NumSdc; j++)
			CD->Totalconc[j][i] += CD->Dependency[i - CD->NumStc][j];
	fprintf(stderr, " \n Dependency Matrix!\n\t\t");

	for (i = 0; i < CD->NumSdc; i++)
		fprintf(stderr, "%6s\t", CD->chemtype[i].ChemName);
	fprintf(stderr, "\n");

	for (i = 0; i < CD->NumSsc; i++)  /* Number of secondary speices in the simulator */
	{
		fprintf(stderr, " %12s\t", CD->chemtype[i + CD->NumStc].ChemName);
		for (j = 0; j < CD->NumSdc; j++)   /* Number of independent species (others depending on these species) */
			fprintf(stderr, " %6.2f\t", CD->Dependency[i][j]);
		fprintf(stderr, " %6.2f\n", CD->Keq[i]); 
	}

	fprintf(stderr, " \n Total Concentration Matrix!\n\t\t");
	for (i = 0; i < CD->NumStc + CD->NumSsc; i++)
		fprintf(stderr, "%6s\t", CD->chemtype[i].ChemName);
	fprintf(stderr, "\n");
	for (i = 0; i < CD->NumStc; i++) {
		fprintf(stderr, " Sum%12s\t", CD->chemtype[i].ChemName);
		for (j = 0; j < CD->NumStc + CD->NumSsc; j++)
			fprintf(stderr, " %6.2f\t", CD->Totalconck[i][j] = CD->Totalconc[i][j]);
		fprintf(stderr, "\n");
	}

	fprintf(stderr, " \n Kinetic Mass Matrx!\n\t\t");
	for (i = 0; i < CD->NumStc; i++)
		fprintf(stderr, " %6s\t", CD->chemtype[i].ChemName);
	fprintf(stderr, "\n");
	for (j = 0; j < CD->NumMkr + CD->NumAkr; j++) {
		fprintf(stderr, " %6s\t", CD->chemtype[j + CD->NumSpc + CD->NumAds + CD->NumCex].ChemName);
		for (i = 0; i < CD->NumStc; i++) {
			fprintf(stderr, " %6.2f\t", CD->Dep_kinetic[j][i]);
		}
		fprintf(stderr, "\tKeq = %6.2f\n", CD->KeqKinect[j]);
	}
	for (i = 0; i < WORDS_LINE; i++)
		free(tmpstr[i]);
	free(tmpstr);

}

int Speciation(Chem_Data CD, int cell) {

	/* if speciation flg = 1, pH is defined
	* if speciation flg = 0, all defined value is total concentration */

	int i, j, k, control, speciation_flg = CD->SPCFlg, num_spe = CD->NumStc + CD->NumSsc;
	double residue[CD->NumStc], residue_t[CD->NumStc], tmpconc[CD->NumStc + CD->NumSsc], totconc[CD->NumStc];
	double tmpval, tmpprb = 1E-2, I, Iroot;
	double error[CD->NumStc], gamma[num_spe], Keq[CD->NumSsc], current_totconc[CD->NumStc], adh, bdh, bdt;
	realtype ** jcb;


	if (CD->TEMcpl == 0) {
		for (i = 0; i < CD->NumSsc; i++)
			Keq[i] = CD->Keq[i];
		adh = CD->DH.adh;
		bdh = CD->DH.bdh;
		bdt = CD->DH.bdt;
	}
	/*
	if  ( CD->TEMcpl == 1){
	for ( i = 0 ; i < CD->NumSsc; i ++)
	Keq[i] = CD->Vcele[cell].Keq[i];
	adh = CD->Vcele[cell].DH.adh;
	bdh = CD->Vcele[cell].DH.bdh;
	bdt = CD->Vcele[cell].DH.bdt;
	}
	*/
	//  fprintf(stderr, "\n The activity correction is set to %d.\n", CD->ACTmod);
	for (i = 0; i < CD->NumStc; i++) {
		tmpconc[i] = log10(CD->Vcele[cell].p_conc[i]);
	}
	// using log10 conc as the primary unknowns. works better because negative numbers are not problem.
	for (i = 0; i < CD->NumSsc; i++) {
		tmpval = 0.0;
		for (j = 0; j < CD->NumSdc; j++) {
			tmpval += tmpconc[j] * CD->Dependency[i][j];
		}
		tmpval -= Keq[i];
		tmpconc[i + CD->NumStc] = tmpval;
		//    fprintf(stderr, " %s %6.4f\t", CD->chemtype[i+CD->NumSpc].ChemName, tmpconc[i+CD->NumSpc]);
	}
	// initial speciation to get secondary species, no activity corrections
	//  if ( CD->ACTmod !=1)
	for (i = 0; i < num_spe; i++)
		gamma[i] = 0;

	for (i = 0; i < CD->NumStc; i++) {
		//    fprintf(stderr, " Sum%s:%12.10f\t %s:%12.10f\n",CD->chemtype[i].ChemName, log10(CD->Vcele[cell].t_conc[i]), CD->chemtype[i].ChemName,log10( CD->Vcele[cell].p_conc[i]));
		current_totconc[i] = CD->Vcele[cell].t_conc[i];
	}

	if (speciation_flg == 1) {
		/* pH is defined, total concentration is calculated from the activity of H */
		/* Dependency is the same but the total concentration for H need not be solved */
		// jcb = denalloc(CD->NumStc -1);  // 09.17
		jcb = newDenseMat(CD->NumStc - 1, CD->NumStc - 1);  // 09.17
		long int p[CD->NumStc - 1];
		realtype x_[CD->NumStc - 1];
		double  maxerror = 1;
		control = 0;
		while (maxerror > TOL) {
			if (CD->ACTmod == 1) {
				I = 0;
				// calculate the ionic strength in this block
				for (i = 0; i < num_spe; i++)
					I += 0.5 * pow(10, tmpconc[i]) * sqr(CD->chemtype[i].Charge);
				Iroot = sqrt(I);
				for (i = 0; i < num_spe; i++) {
					if (CD->chemtype[i].itype == 4) gamma[i] = -tmpconc[i];
					// aqueous species in the unit of mol/L, however the solids are in the unit of mol/L porous media
					// the activity of solid is 1, the log10 of activity is 0.
					// by assigning gamma[minerals] to negative of the tmpconc[minerals], we ensured the log 10 of activity of solids are 0;

					else gamma[i] = (-adh * sqr(CD->chemtype[i].Charge) * Iroot) / (1 + bdh * CD->chemtype[i].SizeF * Iroot) + bdt * I;
					// fprintf(stderr, " Log10gamma of %s %6.4f\n", CD->chemtype[i].ChemName, gamma[i]);
					if (strcmp(CD->chemtype[i].ChemName, "'H+'") == 0) {
						tmpconc[i] = log10(CD->Vcele[cell].p_actv[i]) - gamma[i];
						// log a  = log c + log gamma; log c = log a - log gamma;
					}
				}
				// gamma stores log10gamma[i].
			}
			//      fprintf(stderr, "\n Ionic strength is %6.4f\n", I);
			//      fprintf(stderr, " Newton Iterations = %d\n\n",control);
			for (i = 0; i < CD->NumSsc; i++) {
				tmpval = 0.0;
				for (j = 0; j < CD->NumSdc; j++) {
					tmpval += (tmpconc[j] + gamma[j])* CD->Dependency[i][j];
				}
				tmpval -= Keq[i] + gamma[i + CD->NumStc];
				tmpconc[i + CD->NumStc] = tmpval;
				//	fprintf(stderr, " UPDATE %s %6.4f\n", CD->chemtype[i+CD->NumSpc].ChemName, tmpconc[i+CD->NumSpc]);
			}
			for (i = 0; i < CD->NumStc; i++) {
				tmpval = 0.0;
				for (j = 0; j < num_spe; j++) {
					tmpval += CD->Totalconc[i][j] * pow(10, tmpconc[j]);
					//fprintf(stderr, " %s %6.4f\t %6.4f\n", CD->chemtype[j].ChemName,CD->Totalconc[i][j], tmpconc[j]);
				}
				totconc[i] = tmpval;
				if (strcmp(CD->chemtype[i].ChemName, "'H+'") == 0)
					CD->Vcele[cell].t_conc[i] = totconc[i];
				residue[i] = tmpval - CD->Vcele[cell].t_conc[i];
				/* update the total concentration of H+ for later stage RT at initialization */
				// fprintf(stderr, " Residue: Sum%s %6.4g\n", CD->chemtype[i].ChemName, residue[i]);
			}
			int row, col;
			col = 0;
			for (k = 0; k < CD->NumStc; k++) {
				if (strcmp(CD->chemtype[k].ChemName, "'H+'") != 0) {
					tmpconc[k] += tmpprb;
					for (i = 0; i < CD->NumSsc; i++) {
						tmpval = 0.0;
						for (j = 0; j < CD->NumSdc; j++)
							tmpval += (tmpconc[j] + gamma[j])* CD->Dependency[i][j];
						tmpval -= Keq[i] + gamma[i + CD->NumStc];
						tmpconc[i + CD->NumStc] = tmpval;
						//  fprintf(stderr, " CALC_SEC %s %6.4f\t %6.4f\n", CD->chemtype[i+CD->NumStc].ChemName, tmpval, pow(10,tmpconc[i+CD->NumStc]));
					}
					row = 0;
					for (i = 0; i < CD->NumStc; i++) {
						if (strcmp(CD->chemtype[i].ChemName, "'H+'") != 0) {
							tmpval = 0.0;
							for (j = 0; j < CD->NumStc + CD->NumSsc; j++) {
								tmpval += CD->Totalconc[i][j] * pow(10, tmpconc[j]);
							}
	            residue_t[i] = tmpval - CD->Vcele[cell].t_conc[i];
							jcb[col][row] = (residue_t[i] - residue[i]) / (tmpprb);
							row++;
						}
					}
					tmpconc[k] -= tmpprb;
					col++;
				}
			}
			row = 0;
			for (i = 0; i < CD->NumStc; i++)
				if (strcmp(CD->chemtype[i].ChemName, "'H+'") != 0)
					x_[row++] = -residue[i];
 
			if (denseGETRF(jcb, CD->NumStc - 1, CD->NumStc - 1, p) != 0) {   
				ReportError(CD->Vcele[cell], CD);
				return(1);
				//  assert(gefa(jcb, CD->NumStc-1, p) == 0 );
			}
			denseGETRS(jcb, CD->NumStc - 1, p, x_);  

			row = 0;
			for (i = 0; i < CD->NumStc; i++) {
				if (strcmp(CD->chemtype[i].ChemName, "'H+'") != 0)
					tmpconc[i] += x_[row++];
				error[i] = residue[i] / totconc[i];
			}
			maxerror = fabs(error[0]);
			for (i = 1; i < CD->NumStc; i++)
				if (fabs(error[i]) > maxerror) maxerror = fabs(error[i]);
			control++;
		}
	}
	if (speciation_flg == 0) {
		jcb = newDenseMat(CD->NumStc, CD->NumStc);  
		long int p[CD->NumStc];
		realtype x_[CD->NumStc];
		control = 0;
		double  maxerror = 1;
		while (maxerror > TOL) {
			if (CD->ACTmod == 1) {
				I = 0.0;
				// calculate the ionic strength in this block
				for (i = 0; i < num_spe; i++) {
					I += 0.5 * pow(10, tmpconc[i]) * sqr(CD->chemtype[i].Charge);
				}
				Iroot = sqrt(I);
				for (i = 0; i < num_spe; i++) {
					if (CD->chemtype[i].itype == 4) gamma[i] = -tmpconc[i];
					else gamma[i] = (-adh * sqr(CD->chemtype[i].Charge) * Iroot) / (1 + bdh * CD->chemtype[i].SizeF * Iroot) + bdt * I;                          
				}
			}

			for (i = 0; i < CD->NumSsc; i++) {
				tmpval = 0.0;
				for (j = 0; j < CD->NumSdc; j++) {
					tmpval += (tmpconc[j] + gamma[j])* CD->Dependency[i][j];
				}
				tmpval -= Keq[i] + gamma[i + CD->NumStc];
				tmpconc[i + CD->NumStc] = tmpval;
			}
			for (i = 0; i < CD->NumStc; i++) {
				tmpval = 0.0;
				for (j = 0; j < CD->NumStc + CD->NumSsc; j++) {
					tmpval += CD->Totalconc[i][j] * pow(10, tmpconc[j]);
				}
				totconc[i] = tmpval;
				residue[i] = tmpval - CD->Vcele[cell].t_conc[i];
			}

			for (k = 0; k < CD->NumStc; k++) {
				tmpconc[k] += tmpprb;
				for (i = 0; i < CD->NumSsc; i++) {
					tmpval = 0.0;
					for (j = 0; j < CD->NumSdc; j++)
						tmpval += (tmpconc[j] + gamma[j])* CD->Dependency[i][j];
					tmpval -= Keq[i] + gamma[i + CD->NumStc];
					tmpconc[i + CD->NumStc] = tmpval;
				}
				for (i = 0; i < CD->NumStc; i++) {
					tmpval = 0.0;
					for (j = 0; j < CD->NumStc + CD->NumSsc; j++) {
						tmpval += CD->Totalconc[i][j] * pow(10, tmpconc[j]);
					}

					residue_t[i] = tmpval - CD->Vcele[cell].t_conc[i];
					jcb[k][i] = (residue_t[i] - residue[i]) / (tmpprb);
				}
				tmpconc[k] -= tmpprb;
			}
			for (i = 0; i < CD->NumStc; i++)
				x_[i] = -residue[i];

			if (denseGETRF(jcb, CD->NumStc, CD->NumStc, p) != 0) {   

				ReportError(CD->Vcele[cell], CD);
				return(1);
				//	assert(gefa(jcb, CD->NumStc, p) == 0 );
			}

			denseGETRS(jcb, CD->NumStc, p, x_);   

			for (i = 0; i < CD->NumStc; i++) {
				tmpconc[i] += x_[i];
				error[i] = residue[i] / totconc[i];
			}
			maxerror = fabs(error[0]);
			for (i = 1; i < CD->NumStc; i++)
				if (fabs(error[i]) > maxerror) maxerror = fabs(error[i]);
			control++;
		}
	}

	for (i = 0; i < CD->NumSsc; i++) {
		tmpval = 0.0;
		for (j = 0; j < CD->NumSdc; j++) {
			tmpval += (tmpconc[j] + gamma[j])* CD->Dependency[i][j];
		}
		tmpval -= Keq[i] + gamma[i + CD->NumStc];
		tmpconc[i + CD->NumStc] = tmpval;
		//  fprintf(stderr, " UPDATE %s %6.4f\n", CD->chemtype[i+CD->NumStc].ChemName, (tmpconc[i+CD->NumStc]));                                                     
	}
	for (i = 0; i < CD->NumStc; i++) {
		tmpval = 0.0;
		for (j = 0; j < CD->NumStc + CD->NumSsc; j++) {
			tmpval += CD->Totalconc[i][j] * pow(10, tmpconc[j]);
			//  fprintf(stderr, " %s %6.4f\t %6.4f\n", CD->chemtype[j].ChemName,CD->Totalconc[i][j], tmpconc[j]);                                                        
		}
		totconc[i] = tmpval;
		residue[i] = tmpval - CD->Vcele[cell].t_conc[i];
		error[i] = residue[i] / totconc[i];
		//  fprintf(stderr, " Residue: Sum%s %6.4f\n", CD->chemtype[i].ChemName, residue[i]);
	}
	for (i = 0; i < CD->NumStc + CD->NumSsc; i++) {
		if (i < CD->NumStc) {
			if (CD->chemtype[i].itype == 4) {
				CD->Vcele[cell].p_conc[i] = pow(10, tmpconc[i]);
				CD->Vcele[cell].p_actv[i] = 1.0;
			}
			else {
				CD->Vcele[cell].p_conc[i] = pow(10, tmpconc[i]);
				CD->Vcele[cell].p_actv[i] = pow(10, (tmpconc[i] + gamma[i]));
			}
		}
		else {
			CD->Vcele[cell].s_conc[i - CD->NumStc] = pow(10, tmpconc[i]);
			CD->Vcele[cell].s_actv[i - CD->NumStc] = pow(10, (tmpconc[i] + gamma[i]));
		}
	}
	destroyMat(jcb);  
	return(0);
}


int React(realtype t, realtype stepsize, Chem_Data  CD, int cell, int * NR_times, const pihm_struct pihm)  
{

	if (CD->Vcele[cell].sat < 1.0E-2) return(0);  // very dry, no reaction can take place.
	int i, j, k, control, speciation_flg = CD->SPCFlg, num_spe = CD->NumStc + CD->NumSsc, min_pos, pivot_flg;
	int mn, in;  
	double monodterm = 1.0, inhibterm = 1.0;  
	double CUE_SOC;  
	double soilmoisture;  
	double fieldcapacity; 
	double fd = 1.0;     
  double ftemp = 1.0;   
	double z_SOC = 0.0;  
	double vb;           
	int stc = CD->NumStc, ssc = CD->NumSsc, spc = CD->NumSpc, sdc = CD->NumSdc, nkr = CD->NumMkr + CD->NumAkr, smc = CD->NumMin;
	double * residue, *residue_t, *tmpconc, *totconc, *area, *error, *gamma, *Keq, *Rate_pre, *IAP, *dependency, *Rate_spe, *Rate_spe_t, *Rate_spet;
  double *area_noSw;  
	residue = (double*)malloc(stc * sizeof(double));
	residue_t = (double*)malloc(stc * sizeof(double));
	tmpconc = (double*)malloc(num_spe * sizeof(double));
	totconc = (double*)malloc(stc * sizeof(double));
	area = (double*)malloc(smc * sizeof(double));
  area_noSw = (double*)malloc(smc * sizeof(double));  
	error = (double*)malloc(stc * sizeof(double));
	gamma = (double*)malloc(num_spe * sizeof(double));
	Keq = (double*)malloc(ssc * sizeof(double));
	Rate_pre = (double*)malloc(nkr * sizeof(double));
	IAP = (double*)malloc(nkr * sizeof(double));
	dependency = (double*)malloc(nkr * sizeof(double));
	Rate_spe = (double*)malloc(stc * sizeof(double));
	Rate_spe_t = (double*)malloc(stc * sizeof(double));
	Rate_spet = (double*)malloc(stc * sizeof(double));
	long int * p = (long int*)malloc((CD->NumStc - CD->NumMin) * sizeof(long int));
	realtype * x_ = (realtype*)malloc((CD->NumStc - CD->NumMin) * sizeof(realtype));
	double tmpval, tmpprb, inv_sat, I, Iroot, tmpKeq, ISupdateflg, adh, bdh, bdt, maxerror = 1, surf_ratio, tot_cec, tmpprb_inv;
	realtype ** jcb;

	pihm_struct pihmReact;
	pihmReact = pihm;
	control = 0;
	tmpprb = 1.0E-2;
	tmpprb_inv = 1.0 / tmpprb;
	inv_sat = 1.0 / CD->Vcele[cell].sat;

	for (i = 0; i < CD->NumMin; i++)
	{ 
    area[i] = CD->CalSSA * CD->Vcele[cell].p_para[i + CD->NumStc - CD->NumMin] * CD->Vcele[cell].p_conc[i + CD->NumStc - CD->NumMin] * CD->chemtype[i + CD->NumStc - CD->NumMin].MolarMass;
    area_noSw[i] = area[i]; 
	}

	if (CD->SUFEFF)
	{
		if (CD->Vcele[cell].sat < 1.0)
		{
			surf_ratio = pow(CD->Vcele[cell].sat, 0.6667);
			for (i = 0; i < CD->NumMin; i++)
			{
				area[i] *= surf_ratio;
        area_noSw[i] *= 1.0;  
			}
		}
	}// Lichtner's 2 third law if SUF_EFF is turned on.

	for (j = 0; j < CD->NumStc; j++)
	{
		Rate_spe[j] = 0.0;
	}

	for (i = 0; i < CD->NumMkr + CD->NumAkr; i++)
	{
		min_pos = CD->kinetics[i].position - CD->NumStc + CD->NumMin;


		if (CD->kinetics[i].type == 1) // TST rate
		{
			IAP[i] = 0.0;
			for (j = 0; j < CD->NumStc; j++)
			{
				IAP[i] += log10(CD->Vcele[cell].p_actv[j])* CD->Dep_kinetic[min_pos][j];
			}
			IAP[i] = pow(10, IAP[i]);
			tmpKeq = pow(10, CD->KeqKinect[min_pos]);
			dependency[i] = 1.0;
			for (k = 0; k < CD->kinetics[i].num_dep; k++)
				dependency[i] *= pow(CD->Vcele[cell].p_actv[CD->kinetics[i].dep_position[k]], CD->kinetics[i].dep_power[k]);
			/* Calculate the predicted rate depending on the type of rate law!  */
			Rate_pre[i] = area[min_pos] * (pow(10, CD->kinetics[i].rate))* dependency[i] * (1 - (IAP[i] / tmpKeq)) * 60;
			/* Rate_pre: in mol/L water       / min   rate per reaction
			area: m2/L water
			rate: mol/m2/s
			dependency: dimensionless;
			*/
		}

		else if (CD->kinetics[i].type == 4) // Monod rate
		{    
      monodterm = 1.0;  
      inhibterm = 1.0; 
      
      // soil temperature factor
      ftemp = pow(2.0, (CD->Vcele[cell].temperature - 273.15 - 20)/10);

			// calculate rate
			for (mn = 0; mn < CD->kinetics[i].num_monod; mn++)
			{
				monodterm *= CD->Vcele[cell].p_conc[CD->kinetics[i].monod_position[mn]] / (CD->Vcele[cell].p_conc[CD->kinetics[i].monod_position[mn]] + CD->kinetics[i].monod_para[mn]);
			}

			for (in = 0; in < CD->kinetics[i].num_inhib; in++)
			{
				inhibterm *= CD->kinetics[i].inhib_para[in] / (CD->kinetics[i].inhib_para[in] + CD->Vcele[cell].p_conc[CD->kinetics[i].inhib_position[in]]);
			}
      
			if (cell <= CD->NumEle - 1)  // saturated zone
			{
        z_SOC = CD->Vcele[cell].maxwater - (CD->Vcele[cell].height_t + CD->Vcele[cell + CD->NumEle].height_t);
			}
			else  // unsaturated zone
			{
        z_SOC = CD->Vcele[cell].maxwater - (CD->Vcele[cell].height_t + CD->Vcele[cell - CD->NumEle].height_t);
			}

			if (z_SOC > 0)
				z_SOC = z_SOC;
			else
				z_SOC = 0.0;
			fd = 1.0 * exp(-z_SOC / 0.10);
      
      // based on CrunchTope
      Rate_pre[i] = ftemp * area[min_pos] * pow(10, CD->kinetics[i].rate) * fd * 60;
		}

		for (j = 0; j < CD->NumStc; j++)
		{
			Rate_spe[j] += Rate_pre[i] * CD->Dep_kinetic[min_pos][j];
		}
	}




	for (i = 0; i < CD->NumMkr + CD->NumAkr; i++)
	{
		min_pos = CD->kinetics[i].position - CD->NumStc + CD->NumMin;
		if (Rate_pre[i] < 0.0)
		{
			if (CD->Vcele[cell].p_conc[min_pos + CD->NumStc - CD->NumMin] < 1.0E-8)  // mineral cutoff when mineral is disappearing.
				area[min_pos] = 0.0;
		}
	}

	for (i = 0; i < CD->NumSpc; i++)
		if (CD->chemtype[i].itype == 1)   // aqueous species, saturation term for aqueous volume 
			Rate_spe[i] = Rate_spe[i] * inv_sat;

	jcb = newDenseMat(CD->NumStc - CD->NumMin, CD->NumStc - CD->NumMin);  
																		  /*
																		  long int p[CD->NumStc];
																		  realtype x_[CD->NumStc];
																		  control =0;
																		  double  maxerror= 1;
																		  */
	if (CD->TEMcpl == 0) {
		for (i = 0; i < CD->NumSsc; i++)
			Keq[i] = CD->Keq[i];
		adh = CD->DH.adh;
		bdh = CD->DH.bdh;
		bdt = CD->DH.bdt;
	}

	for (i = 0; i < CD->NumStc; i++) {
		tmpconc[i] = log10(CD->Vcele[cell].p_conc[i]);
	}
	for (i = 0; i < CD->NumSsc; i++) {
		tmpconc[i + CD->NumStc] = log10(CD->Vcele[cell].s_conc[i]);
	}
	tot_cec = 0.0;
	for (i = 0; i < num_spe; i++) {
		if (CD->chemtype[i].itype == 3) {
			tot_cec += pow(10, tmpconc[i]);
		}
	}

	I = 0;
	for (i = 0; i < num_spe; i++) {
		I += 0.5 * pow(10, tmpconc[i]) * sqr(CD->chemtype[i].Charge);
	}
	Iroot = sqrt(I);
	for (i = 0; i < num_spe; i++) {
		switch (CD->chemtype[i].itype) {
		case 1:
			gamma[i] = (-adh * sqr(CD->chemtype[i].Charge) * Iroot) / (1 + bdh * CD->chemtype[i].SizeF * Iroot) + bdt * I;
			break;
		case 2:   gamma[i] = log10(CD->Vcele[cell].sat);   break;
		case 3:   gamma[i] = -log10(tot_cec);   break;
		case 4:   gamma[i] = -tmpconc[i];       break;
		}
	}

	while (maxerror > TOL) 
	{
		for (i = 0; i < CD->NumSsc; i++) 
		{
			tmpval = 0.0;
			for (j = 0; j < CD->NumSdc; j++) 
			{
				tmpval += (tmpconc[j] + gamma[j])* CD->Dependency[i][j];
			}
			tmpval -= Keq[i] + gamma[i + CD->NumStc];
			tmpconc[i + CD->NumStc] = tmpval;
			//   fprintf(stderr, " UPDATE %s %6.4f\n", CD->chemtype[i+CD->NumStc].ChemName, tmpconc[i+CD->NumStc]);
		}

		for (j = 0; j < CD->NumStc; j++) 
		{
			Rate_spet[j] = 0.0;
		}

		for (i = 0; i < CD->NumMkr + CD->NumAkr; i++) 
		{
			min_pos = CD->kinetics[i].position - CD->NumStc + CD->NumMin;
			//  fprintf(stderr, " Min_pos %d\n", min_pos);

			if (CD->kinetics[i].type == 1) // 08.20, TST rate
			{
				IAP[i] = 0.0;
				//  fprintf(stderr, " IAP_BC %s %6.4f\n", CD->kinetics[i].Label, IAP[i]);
				for (j = 0; j < CD->NumStc; j++) 
				{
					if (CD->chemtype[j].itype != 4) 
					{
						IAP[i] += (tmpconc[j] + gamma[j]) * CD->Dep_kinetic[min_pos][j];
						//  fprintf(stderr, " IAP_CALC, %s %6.4f\t %6.4f\n" ,CD->chemtype[j].ChemName, tmpconc[j]+gamma[j], CD->Dep_kinetic[min_pos][j]);
					}
				}
				IAP[i] = pow(10, IAP[i]);
				tmpKeq = pow(10, CD->KeqKinect[min_pos]);
				/*
				if ( IAP[i] < tmpKeq)
				rct_drct[i] = 1.0;
				if ( IAP[i] > tmpKeq)
				rct_drct[i] = -1.0;
				if ( IAP[i] == tmpKeq)
				rct_drct[i] = 0.0;
				*/
				//    fprintf(stderr, " IAP_BC %s %6.4f\n", CD->kinetics[i].Label, IAP[i]);
				dependency[i] = 0.0;
				for (k = 0; k < CD->kinetics[i].num_dep; k++)
					dependency[i] += (tmpconc[CD->kinetics[i].dep_position[k]] + gamma[CD->kinetics[i].dep_position[k]])* CD->kinetics[i].dep_power[k];
				dependency[i] = pow(10, dependency[i]);
				//  fprintf(stderr, " Dep: %6.4f\n", dependency[i]);
				/* Calculate the predicted rate depending on the type of rate law!  */
				Rate_pre[i] = area[min_pos] * (pow(10, CD->kinetics[i].rate))* dependency[i] * (1 - (IAP[i] / tmpKeq)) * 60;
				/* Rate_pre: in mol/L water       / min
				area: m2/L water
				rate: mol/m2/s
				dependency: dimensionless;
				*/
			}

			else if (CD->kinetics[i].type == 4) 
			{
        monodterm = 1.0;
        inhibterm = 1.0;
           
				// calculate rate
				for (mn = 0; mn < CD->kinetics[i].num_monod; mn++)
				{
					monodterm *= CD->Vcele[cell].p_conc[CD->kinetics[i].monod_position[mn]] / (CD->Vcele[cell].p_conc[CD->kinetics[i].monod_position[mn]] + CD->kinetics[i].monod_para[mn]);
				}

				for (in = 0; in < CD->kinetics[i].num_inhib; in++)
				{
					inhibterm *= CD->kinetics[i].inhib_para[in] / (CD->kinetics[i].inhib_para[in] + CD->Vcele[cell].p_conc[CD->kinetics[i].inhib_position[in]]);
				}
             
				if (cell <= CD->NumEle - 1)  // saturated zone
				{
          z_SOC = CD->Vcele[cell].maxwater - (CD->Vcele[cell].height_t + CD->Vcele[cell + CD->NumEle].height_t);
				}
				else  // unsaturated zone
				{  
          z_SOC = CD->Vcele[cell].maxwater - (CD->Vcele[cell].height_t + CD->Vcele[cell - CD->NumEle].height_t);      
				}

				if (z_SOC > 0)
					z_SOC = z_SOC;
				else
					z_SOC = 0.0;
				fd = 1.0 * exp(-z_SOC / 0.10);
				//printf(" Calculation [Cell,%d]: height_v = %f, z_DOC = %f, fd = %f \n", cell, CD->Vcele[i].height_v, z_SOC, fd);  // 11.28 check fd

        Rate_pre[i] = ftemp * area[min_pos] * pow(10, CD->kinetics[i].rate) * fd * 60;
			}

			for (j = 0; j < CD->NumStc; j++) 
			{
				Rate_spet[j] += Rate_pre[i] * CD->Dep_kinetic[min_pos][j];
				//fprintf(stderr, " Rate for %s: %6.4g\n", CD->chemtype[j].ChemName, Rate_pre[i]*CD->Dep_kinetic[min_pos][j]);
			}
			// Adjust the unit of the calcuated rate. Note that for mineral, the unit of rate and the unit of concentration are mol/L porous media
			// for the aqueous species, the unit of the rate and the unit of the concentration are mol/L pm and mol/L water respectively.

		}
		/*for ( i = 0 ; i < CD->NumSpc; i ++){
		// fprintf(stderr, " Tot_rate for %s: %6.4g\n", CD->chemtype[i].ChemName, Rate_spet[i]);
		Rate_spet[i] = Rate_spet[i]/(CD->Vcele[cell].porosity * CD->Vcele[cell].sat);
		// fprintf(stderr, " Tot_rate for %s: %6.4g\n", CD->chemtype[i].ChemName, Rate_spet[i]);
		}*/

		for (i = 0; i < CD->NumSpc; i++)
			if (CD->chemtype[i].itype == 1)
				Rate_spet[i] = Rate_spet[i] * inv_sat;


		for (i = 0; i < CD->NumStc - CD->NumMin; i++) 
		{
			tmpval = 0.0;
			for (j = 0; j < CD->NumStc + CD->NumSsc; j++) 
			{
				tmpval += CD->Totalconc[i][j] * pow(10, tmpconc[j]);
				//fprintf(stderr, " %s %6.4f\t %6.4f\n", CD->chemtype[j].ChemName,CD->Totalconc[i][j], tmpconc[j]);
			}
			totconc[i] = tmpval;
			residue[i] = tmpval - (CD->Vcele[cell].t_conc[i] + (Rate_spe[i] + Rate_spet[i])*stepsize *0.5);
			//residue[i] = tmpval -( CD->Vcele[cell].t_conc[i] + (Rate_spet[i])*stepsize);
			//      fprintf(stderr, " %s Residue: %6.4g\t Sum1:%6.4g\tSum2:%6.4g\tRate1%6.4g\tRate2%6.4g\n", CD->chemtype[i].ChemName, residue[i],totconc[i], CD->Vcele[cell].t_conc[i], Rate_spe[i], Rate_spet[i]);
		}
		if (control % SKIP_JACOB == 0)  // update jacobian every the other iteration
		{
			for (k = 0; k < CD->NumStc - CD->NumMin; k++) {
				tmpconc[k] += tmpprb;
				for (i = 0; i < CD->NumSsc; i++) {
					tmpval = 0.0;
					for (j = 0; j < CD->NumSdc; j++)
						tmpval += (tmpconc[j] + gamma[j])* CD->Dependency[i][j];
					tmpval -= Keq[i] + gamma[i + CD->NumStc];
					tmpconc[i + CD->NumStc] = tmpval;
					// fprintf(stderr, " CALC_SEC %s %6.4f\t %6.4f\n", CD->chemtype[i+CD->NumSpc].ChemName, tmpval, pow(10,tmpconc[i+CD->NumSpc]));
				}
				for (i = 0; i < CD->NumStc - CD->NumMin; i++) {
					tmpval = 0.0;
					for (j = 0; j < CD->NumStc + CD->NumSsc; j++) {
						tmpval += CD->Totalconc[i][j] * pow(10, tmpconc[j]);
						// fprintf(stderr, " CALC_TOT %s %6.4f\t %6.4f\n",CD->chemtype[j].ChemName, CD->Totalconc[i][j], pow(10,tmpconc[j]));
					}
					//	  totconc[i] = tmpval;
					//fprintf(stderr, " CALC_TOT Sum%s %6.4f\t %12.10f\n", CD->chemtype[i].ChemName, log10(totconc[i]), totconc[i]);
					residue_t[i] = tmpval - (CD->Vcele[cell].t_conc[i] + (Rate_spe[i] + Rate_spet[i])*stepsize *0.5);
					jcb[k][i] = (residue_t[i] - residue[i]) * tmpprb_inv;

					//	fprintf(stderr, "%g\n", jcb[k][i]);
					//	fprintf(stderr, " Sum%s %6.4f\t %6.4f\n", CD->chemtype[i].ChemName, tmpval, residue_t[i]);
				}
				tmpconc[k] -= tmpprb;
			}
		}
		for (i = 0; i < CD->NumStc - CD->NumMin; i++)
			x_[i] = -residue[i];
		pivot_flg = denseGETRF(jcb, CD->NumStc - CD->NumMin, CD->NumStc - CD->NumMin, p);   
		if (pivot_flg != 0) {
			CD->Vcele[cell].illness++;
			return(1);
		}

		denseGETRS(jcb, CD->NumStc - CD->NumMin, p, x_); 

		for (i = 0; i < CD->NumStc - CD->NumMin; i++) {
			if (fabs(x_[i]) < 0.3)
				tmpconc[i] += x_[i];
			else {
				if (x_[i] < 0)
					tmpconc[i] += -0.3;
				else
					tmpconc[i] += 0.3;
			}
			//  fprintf(stderr, " %s  TMPCON %6.4f\t IMPROVE %g\n", CD->chemtype[i].ChemName, tmpconc[i], x_[i]);
			error[i] = residue[i] / totconc[i];
			// fprintf(stderr, "  RESI %6.4g\t TOT_CONC %6.4g\t ERROR %6.4g\n", residue[i], totconc[i], error[i] );
		}
		maxerror = fabs(error[0]);
		for (i = 1; i < CD->NumStc - CD->NumMin; i++)
			if (fabs(error[i]) > maxerror) maxerror = fabs(error[i]);
		control++;
		if (control > 10) return(1);  
	}

	*(NR_times) = control;
	destroyMat(jcb);  

	for (i = 0; i < CD->NumSsc; i++) {
		tmpval = 0.0;
		for (j = 0; j < CD->NumSdc; j++) {
			tmpval += (tmpconc[j] + gamma[j])* CD->Dependency[i][j];
		}
		tmpval -= Keq[i] + gamma[i + CD->NumStc];
		tmpconc[i + CD->NumStc] = tmpval;
		//   fprintf(stderr, " UPDATE %s %6.4f\n", CD->chemtype[i+CD->NumStc].ChemName, (tmpconc[i+CD->NumStc]));
	}

	for (i = 0; i < CD->NumStc - CD->NumMin; i++) {
		tmpval = 0.0;
		for (j = 0; j < CD->NumStc + CD->NumSsc; j++) {
			tmpval += CD->Totalconc[i][j] * pow(10, tmpconc[j]);
		}
		totconc[i] = tmpval;
		residue[i] = tmpval - CD->Vcele[cell].t_conc[i];
		error[i] = residue[i] / totconc[i];
	}

	for (i = 0; i < CD->NumStc + CD->NumSsc; i++) {
		/*    if ( tmpconc[i]!=tmpconc[i]){
		fprintf(stderr, " Nan type error from reaction at cell %d, species %d", cell+1, i+1);
		ReportError(CD->Vcele[cell], CD);
		CD->Vcele[cell].illness += 20;
		}*/
		if (i < CD->NumStc) {
			if (CD->chemtype[i].itype == 4) {
				CD->Vcele[cell].t_conc[i] += (Rate_spe[i] + Rate_spet[i])*stepsize *0.5;
				CD->Vcele[cell].p_actv[i] = 1.0;
				CD->Vcele[cell].p_conc[i] = CD->Vcele[cell].t_conc[i];
			}
			else {
				CD->Vcele[cell].p_conc[i] = pow(10, tmpconc[i]);
				//printf(" CD->NumStc + CD->NumSsc = %d \n", CD->NumStc + CD->NumSsc);
				//printf(" [Cell = %d] gamma (i = %d) = %6.4f, tmpconc[i] = %6.4f \n", cell, i, gamma[i], tmpconc[i]);
				CD->Vcele[cell].p_actv[i] = pow(10, (tmpconc[i] + gamma[i]));
				CD->Vcele[cell].t_conc[i] = totconc[i];
			}
		}
		else {
			CD->Vcele[cell].s_conc[i - CD->NumStc] = pow(10, tmpconc[i]);
			CD->Vcele[cell].s_actv[i - CD->NumStc] = pow(10, (tmpconc[i] + gamma[i]));
		}
		//    fprintf(stderr, " %s LogC: %6.4f\t C: %6.4f\t Loga: %6.4f\t a: %6.4f\n", CD->chemtype[i].ChemName, tmpconc[i], pow(10, tmpconc[i]), tmpconc[i]+gamma[i], pow(10,(tmpconc[i]+gamma[i])));
	}

	free(residue);
	free(residue_t);
	free(tmpconc);
	free(totconc);
	free(area);
	free(error);
	free(gamma);
	free(Keq);
	free(Rate_pre);
	free(IAP);
	free(dependency);
	free(Rate_spe);
	free(Rate_spe_t);
	free(Rate_spet);
	free(p);
	free(x_);
	return(0);
}