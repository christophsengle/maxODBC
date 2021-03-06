/**
@file
maxODBC - connect to ODBC datasources with Max

@ingroup database
@author Christoph Sengle (christoph.sengle@web.de)
@version 0.1
@todo testing, rpc, pooling
*/

#include <iostream>
#include <stdio.h>
#include <vector>

#define OTL_ODBC // Compile OTL 4.0/ODBC
#include "otlv4.h" // include the OTL 4.0 header file

//max
#include "ext.h"
#include "ext_obex.h"
#include "ext_strings.h"
#include "commonsyms.h"

using namespace std;

// max object instance data
typedef struct _maxODBC {
	//general
	t_object			c_box;
	vector<void*>		proxy;
	long				proxyData;

	//variables, objects
	otl_connect			db;
	otl_stream*			stream;
	int					streamBuffSize;

	t_symbol			*odbcName;
	t_symbol			*username;
	t_symbol			*password;

	t_string			*queryString;

	//outlets
	void				*resultSetOut;
} t_maxODBC;


// prototypes
void	*maxODBC_new(t_symbol *s, long argc, t_atom *argv);
void	maxODBC_free(t_maxODBC *x);
void	maxODBC_assist(t_maxODBC *x, void *b, long m, long a, char *s);

//input data
void	maxODBC_bang(t_maxODBC *x);
void	maxODBC_int(t_maxODBC *x, long value);
void	maxODBC_float(t_maxODBC *x, float value);
void	maxODBC_list(t_maxODBC *x, t_symbol *msg, long argc, t_atom *argv);
void	maxODBC_anything(t_maxODBC *x, t_symbol *msg, long argc, t_atom *argv);

//attributes
t_max_err attrGetOdbc(t_maxODBC *x, void *attr, long *ac, t_atom **av);
t_max_err attrSetOdbc(t_maxODBC *x, void *attr, long ac, t_atom *av);
t_max_err attrGetUser(t_maxODBC *x, void *attr, long *ac, t_atom **av);
t_max_err attrSetUser(t_maxODBC *x, void *attr, long ac, t_atom *av);
t_max_err attrGetPassword(t_maxODBC *x, void *attr, long *ac, t_atom **av);
t_max_err attrSetPassword(t_maxODBC *x, void *attr, long ac, t_atom *av);

//myfunctions
void		connectDb(t_maxODBC *x);
t_string*	buildQueryString(t_maxODBC *x, t_symbol *msg, long argc, t_atom *argv);
bool		streamInit(t_maxODBC *x, t_string *newQuery=nullptr);
void		addToStream(t_maxODBC *x, t_symbol *msg, long argc, t_atom *argv);
void		addToStream(t_maxODBC *x, t_atom *atom);
void		flushStream(t_maxODBC *x);
void		read(t_maxODBC *x);
void		describeSelect(t_maxODBC *x, vector<t_atom> &tableNames);
void		atomToStream(otl_stream &stream, t_atom* data, int expectedVarType);
void		streamToAtom(t_maxODBC *x, otl_stream &stream, int expectedVarType, vector<t_atom> &outdata);
void		outputResults(t_maxODBC *x, vector<t_atom> &columnNames, vector<vector<t_atom>> &resultSet);

// globals
static t_class	*s_maxODBC_class = NULL;

/************************************************************************************/

void ext_main(void *r)
{
	t_class	*c = class_new("maxODBC",
		(method)maxODBC_new,
		(method)maxODBC_free,
		sizeof(t_maxODBC),
		(method)NULL,
		A_GIMME,
		0);

	//input data
	class_addmethod(c, (method)maxODBC_bang, "bang", 0);
	class_addmethod(c, (method)maxODBC_int, "int", A_LONG, 0);
	class_addmethod(c, (method)maxODBC_float, "float", A_FLOAT, 0);
	class_addmethod(c, (method)maxODBC_list, "list", A_GIMME, 0);
	class_addmethod(c, (method)maxODBC_anything, "anything", A_GIMME, 0);

	//general methods
	class_addmethod(c, (method)maxODBC_assist, "assist", A_CANT, 0);
	class_addmethod(c, (method)stdinletinfo, "inletinfo", A_CANT, 0);

	//attributes
	CLASS_ATTR_SYM(c, "odbcName", 0, t_maxODBC, odbcName);
	CLASS_ATTR_ACCESSORS(c, "odbcName", (method)attrGetOdbc, (method)attrSetOdbc);
	CLASS_ATTR_SYM(c, "username", 0, t_maxODBC, username);
	CLASS_ATTR_ACCESSORS(c, "username", (method)attrGetUser, (method)attrSetUser);
	CLASS_ATTR_SYM(c, "password", 0, t_maxODBC, password);
	CLASS_ATTR_ACCESSORS(c, "password", (method)attrGetPassword, (method)attrSetPassword);

	class_register(CLASS_BOX, c);
	s_maxODBC_class = c;
}


/************************************************************************************/
// Object Creation Method

void *maxODBC_new(t_symbol *s, long argc, t_atom *argv)
{
	t_maxODBC	*x = nullptr;

	if (argc > 0) {
		x = (t_maxODBC *)object_alloc(s_maxODBC_class);
	}
	else {
		post("specify odbc name");
	}

	//check if successfull created
	if (x) {
		//"contructor"
		//default values
		x->stream = new otl_stream();
		x->streamBuffSize = 50;
		x->odbcName = gensym("");
		x->username = gensym("");
		x->password = gensym("");
		x->queryString = string_new("");

		//check args
		for (int i = 0; i < argc; i++) {
			switch (i) {
			case 0:
				x->odbcName = atom_getsym(&argv[0]);
				break;
			case 1:
				x->username = atom_getsym(&argv[1]);
				break;
			case 2:
				x->password = atom_getsym(&argv[2]);
				break;
			}
		}

		//instantiate proxys (reverse order)
		for (int i = 2; i > 0; i--) {
			x->proxy.push_back(proxy_new((t_object *)x, i, &x->proxyData));
		}

		// initialize ODBC environment
		otl_connect::otl_initialize();

		//create outlets, reverse order!
		x->resultSetOut = outlet_new((t_object *)x, NULL);
	}

	connectDb(x);

	return(x);
}

void maxODBC_free(t_maxODBC *x)
{
	x->db.logoff();
	for (size_t i = 0; i < x->proxy.size(); i++) {
		object_free(x->proxy.operator[](i));
	}
}


/************************************************************************************/
// Methods bound to input/inlets

void maxODBC_assist(t_maxODBC *x, void *b, long msg, long arg, char *dst)
{
	if (msg == 1) {
		switch (arg) {
		case 0:
			strcpy(dst, "bang flushes buffer");
			break;
		case 1:
			strcpy(dst, "query data");
			break;
		case 2:
			strcpy(dst, "query statement");
			break;
		}

	}
	else if (msg == 2) {
		switch (arg) {
		case 0:
			strcpy(dst, "bangs if query sent\noutputs result");
		}
	}
}

/* Attributes Set and Get */
t_max_err attrGetUser(t_maxODBC *x, void *attr, long *ac, t_atom **av) {
	if (ac && av) {
		char alloc;

		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setsym(*av, x->username);
	}
	return MAX_ERR_NONE;
}

t_max_err attrSetUser(t_maxODBC *x, void *attr, long ac, t_atom *av) {
	if (ac && av) {
		x->username = atom_getsym(av);
	}

	connectDb(x);
	return MAX_ERR_NONE;
}

t_max_err attrGetPassword(t_maxODBC *x, void *attr, long *ac, t_atom **av) {
	if (ac && av) {
		char alloc;

		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setsym(*av, x->password);
	}
	return MAX_ERR_NONE;
}

t_max_err attrSetPassword(t_maxODBC *x, void *attr, long ac, t_atom *av) {
	if (ac && av) {
		x->password = atom_getsym(av);
	}

	connectDb(x);

	return MAX_ERR_NONE;
}

t_max_err attrGetOdbc(t_maxODBC *x, void *attr, long *ac, t_atom **av) {
	if (ac && av) {
		char alloc;

		if (atom_alloc(ac, av, &alloc)) {
			return MAX_ERR_GENERIC;
		}
		atom_setsym(*av, x->odbcName);
	}
	return MAX_ERR_NONE;
}

t_max_err attrSetOdbc(t_maxODBC *x, void *attr, long ac, t_atom *av) {
	if (ac && av) {
		x->odbcName = atom_getsym(av);
	}

	connectDb(x);

	return MAX_ERR_NONE;
}

/*
Incoming data
*/
void maxODBC_bang(t_maxODBC *x) {
	switch (proxy_getinlet((t_object *)x)) {
	case 0:
	{
		flushStream(x);
		break;
	}
	case 2:
		streamInit(x, x->queryString);
		break;
	default:
		break;
	}
}

void maxODBC_int(t_maxODBC *x, long value) {
	switch (proxy_getinlet((t_object *)x)) {
	case 1:
	{
		t_atom atom;
		atom_setlong(&atom, value);

		addToStream(x, &atom);
		break;
	}
	default:
		break;
	}
}

void maxODBC_float(t_maxODBC *x, float value) {
	switch (proxy_getinlet((t_object *)x)) {
	case 1:
	{
		t_atom atom;
		atom_setfloat(&atom, value);
		addToStream(x, &atom);
		break;
	}
	default:
		break;
	}
}

void maxODBC_list(t_maxODBC *x, t_symbol *msg, long argc, t_atom *argv) {
	switch (proxy_getinlet((t_object *)x)) {
	case 1:
		streamInit(x);
		addToStream(x, msg, argc, argv);
		break;
	default:
		break;
	}
}

//any in
void maxODBC_anything(t_maxODBC *x, t_symbol *msg, long argc, t_atom *argv) {
	switch (proxy_getinlet((t_object *)x)) {
	case 1:
		addToStream(x, msg, argc, argv);
		break;
	case 2:
		streamInit(x, buildQueryString(x, msg, argc, argv));
		break;
	default:
		break;
	}
}

/* other methods */

void connectDb(t_maxODBC *x) {
	try {
		x->db.rlogon(x->username->s_name, x->password->s_name, x->odbcName->s_name);
		post("maxODBC: database connection test completed successfully");
	}
	catch (otl_exception& p) {
		post("maxODBC: database connection test error");
		post((const char*)p.msg); // intercept OTL exceptions
		post((const char*)p.stm_text); // print out SQL that caused the error
		post((const char*)p.sqlstate); // print out SQLSTATE message
		post((const char*)p.var_info); // print out the variable that caused the error
	}
}

t_string* buildQueryString(t_maxODBC *x, t_symbol *msg, long argc, t_atom *argv) {
	t_string* query = string_new(msg->s_name);

	for (long i = 0; i < argc; i++) {
		string_append(query, " ");
		t_atom thisAtom = argv[i];

		if (atom_gettype(&thisAtom) == A_SYM) {
			string_append(query, atom_getsym(&thisAtom)->s_name);
		}
		else {
			char buff[MAX_FILENAME_CHARS] = { '\0' };

			if (atom_gettype(&thisAtom) == A_FLOAT) {
				sprintf(buff, "%f", atom_getfloat(&thisAtom));
			}
			else if (atom_gettype(&thisAtom) == A_LONG) {
				sprintf(buff, "%ld", (long)atom_getlong(&thisAtom));
			}

			string_append(query, buff);
		}
	}

	return query;
}

bool streamInit(t_maxODBC *x, t_string* newQuery) {
	if ((newQuery != nullptr) && (string(string_getptr(newQuery)) != "")) {
		if (x->stream->good()) {
			x->stream->close();
		}

		x->queryString = newQuery;
		return streamInit(x);
	}
	else if (!x->stream->good()) {
		x->stream->open(x->streamBuffSize, string_getptr(x->queryString), x->db);
		return streamInit(x);
	}
	else {
		return true;
	}

	post("maxODBC: no statement specified");
	return false;
}


void addToStream(t_maxODBC *x, t_symbol *msg, long argc, t_atom *argv) {
	for (long i = 0; i < argc; i++) {
		t_atom* thisAtom = (argv + i);
		addToStream(x, thisAtom);
	}
}

void addToStream(t_maxODBC *x, t_atom *atom) {
	if (streamInit(x)) {
		int numOutVars;
		x->stream->describe_out_vars(numOutVars);

		atomToStream(*(x->stream), atom, x->stream->describe_next_in_var()->ftype);
		if (numOutVars > 0) {
			read(x);
		}
	}
	else {
		post("maxODBC: stream not initialized");
	}
}

void streamToAtom(t_maxODBC *x, otl_stream &stream, int expectedVarType, vector<t_atom> &outdata) {
	t_atom atom;

	switch (expectedVarType) {
	case otl_var_int:
	{
		int value = 0;
		stream >> value;
		atom_setlong(&atom, value);
		outdata.push_back(atom);
		break;
	}
	case otl_var_long_int:
	{
		long value = 0;
		stream >> value;
		atom_setlong(&atom, value);
		outdata.push_back(atom);
		break;
	}
	case otl_var_float:
	{
		float value = 0;
		stream >> value;
		atom_setlong(&atom, (t_atom_long)value);
		outdata.push_back(atom);
		break;
	}
	case otl_var_double:
	{
		double value = 0;
		stream >> value;
		atom_setlong(&atom, (t_atom_long)value);
		outdata.push_back(atom);
		break;
	}
	default:
		post("maxODBC: unsupported type");
		break;
	}
}

void atomToStream(otl_stream &stream, t_atom* data, int expectedVarType) {
	switch (expectedVarType) {
	case otl_var_int:
		stream << (int)atom_getlong(data);
		break;
	case otl_var_long_int:
		stream << (long)atom_getlong(data);
		break;
	case otl_var_float:
		stream << (float)atom_getfloat(data);
		break;
	case otl_var_double:
		stream << (double)atom_getfloat(data);
		break;
	default:
		post("maxODBC: unsupported type");
		break;
	}
}

void flushStream(t_maxODBC *x) {
	if (x->stream->good()) {
		x->stream->flush();
		x->stream->close();
		outlet_anything(x->resultSetOut, gensym("bang"), 0, nullptr);
	}
}

void read(t_maxODBC *x) {
	vector<vector<t_atom>> resultSet;
	vector<t_atom> columnNames;

	if (!x->stream->eof()) {
		describeSelect(x, columnNames);
		size_t numColumns = columnNames.size();
		size_t currColumn = 0;

		while (!x->stream->eof()) {
			vector<t_atom> query;

			streamToAtom(x, *(x->stream), x->stream->describe_next_out_var()->ftype, query);

			if (currColumn == (numColumns - 1)) {
				//all columns fetched, append and reset
				resultSet.push_back(query);
				currColumn = 0;
			}
			else {
				currColumn++;
			}
		}

		outputResults(x, columnNames, resultSet);
	}
}

void describeSelect(t_maxODBC *x, vector<t_atom> &tableNames) {
	otl_column_desc* desc;
	int size;

	desc = x->stream->describe_select(size);

	for (int i = 0; i < size; i++) {
		t_atom name;
		atom_setsym(&name, gensym(desc[i].name));

		tableNames.push_back(name);
	}
}

void outputResults(t_maxODBC *x, vector<t_atom> &columnNames, vector<vector<t_atom>>& resultSet) {
	outlet_anything(x->resultSetOut, gensym("list"), columnNames.size(), &columnNames[0]);
	for (size_t i = 0; i < resultSet.size(); i++) {
		vector<t_atom> *thisResult = &resultSet[i];
		outlet_anything(x->resultSetOut, gensym("list"), thisResult->size(), &thisResult->operator[](0));
	}
	
	outlet_anything(x->resultSetOut, gensym("bang"), 0, nullptr);
}

