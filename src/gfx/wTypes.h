#ifndef __CTYPES_H
#define __CTYPES_H


class Size {
public:
	int32_t Width;
	int32_t Height;

	inline Size() {
		Width = 0;
		Height = 0;
	};

	inline Size(int32_t Width, int32_t Height) {
		this->Width = Width;
		this->Height = Height;
	};

	bool operator== (const Size B) {
		if ((Width == B.Width) && (Height == B.Height))
			return true;
		return false;
	};

	bool operator!= (const Size B) {
		if ((Width != B.Width) || (Height != B.Height))
			return true;
		return false;
	};
};

class Point {
public:
	int32_t X;
	int32_t Y;

	inline Point() {
		X = 0;
		Y = 0;
	};

	inline Point(int32_t X, int32_t Y) {
		this->X = X;
		this->Y = Y;
	};

	bool operator== (const Point B) {
		if ((X == B.X) && (Y == B.Y))
			return true;
		return false;
	};

	bool operator!= (const Point B) {
		if ((X != B.X) || (Y != B.Y))
			return true;
		return false;
	};
};


class Rectangle
{
public:

	/*
		Variables de datos
	*/
	union {
		struct {
			int32_t X;
			int32_t Y;
		};
		Point Location;
	};
	union {
		struct {
			int32_t Width;
			int32_t Height;
		};
		class Size Size;
	};

	/*
		Constructores
	*/
	inline Rectangle() {
		X = 0;
		Y = 0;
		Width = 0;
		Height = 0;
	};

	inline Rectangle(int32_t X, int32_t Y, int32_t Width, int32_t Height) {
		this->X = X;
		this->Y = Y;
		this->Width = Width;
		this->Height = Height;
	};

	inline Rectangle(Point P, class Size S) {
		X = P.X;
		Y = P.Y;
		Width = S.Width;
		Height = S.Height;
	};

	inline Rectangle(Point P1, Point P2) {
		X = P1.X;
		Y = P1.Y;
		Width = P2.X - P1.X;
		Height = P2.Y - P1.Y;
	};

	/*
		Propiedades derivadas
	*/

	//Obtiene la coordenada Y del borde superior de esta estructura Rectangle.
	inline int32_t getTop() {
		return Y;
	};

	//Obtiene la coordenada Y que es la suma de los valores de las propiedades Y y Height de esta estructura Rectangle
	inline int32_t getBottom() {
		return Y + Height;
	};

	//Obtiene la coordenada X del borde izquierdo de esta estructura Rectangle.
	inline int32_t getLeft() {
		return X;
	};

	//Obtiene la coordenada X que es la suma de los valores de las propiedades X y Width de esta estructura Rectangle.
	inline int32_t getRight() {
		return X + Width;
	};

	//Comprueba si todas las propiedades numéricas de este Rectangle tienen valores cero.
	bool isEmpty() {
		if ((X == 0) && (Y == 0) && (Width == 0) && (Height == 0))
			return true;
		return false;
	};

	/*
		Métodos
	*/

	//Determina si el punto especificado está dentro de esta estructura de Rectangle.
	bool Contains(int32_t X, int32_t Y) {
		if ((X >= this->X) && (X <= (this->X + Width)) && (Y >= this->Y) && (Y <= (this->Y + Height)))
			return true;
		return false;
	};

	//Determina si el punto especificado está dentro de esta estructura de Rectangle.
	bool Contains(Point P) {
		if ((P.X >= X) && (P.X <= (X + Width)) && (P.Y >= Y) && (P.Y <= (Y + Height)))
			return true;
		return false;
	};

	//Determina si la región rectangular que representa rect está totalmente dentro de esta estructura Rectangle.
	bool Contains(Rectangle rect) {
		return (X <= rect.X) && ((rect.X + rect.Width) <= (X + Width)) &&
			(Y <= rect.Y) && ((rect.Y + rect.Height) <= (Y + Height));
	};

	//Crea una estructura Rectangle con las ubicaciones de los bordes especificadas.
	Rectangle FromLTRB(int32_t left, int32_t top, int32_t right, int32_t bottom) {
		return Rectangle(left, top, right - left, bottom - top);
	};

	//Aumenta este Rectangle en la cantidad especificada.
	void Inflate(int32_t width, int32_t height) {
		X -= width;
		Y -= height;
		Width += 2 * width;
		Height += 2 * height;
	};

	//Aumenta este Rectangle en la cantidad especificada.
	void Inflate(class Size Size) {
		Inflate(Size.Width, Size.Height);
	};

	/*
		Crea y devuelve una copia aumentada de la estructura Rectangle especificada.
		La copia se aumenta en la cantidad especificada. La estructura Rectangle original no cambia.
	*/
	Rectangle Inflate(Rectangle rect, class Size Size) {
		Rectangle x = rect;
		x.Inflate(Size.Width, Size.Height);
		return x;
	};

	/*Devuelve una tercera estructura Rectangle que representa la intersección de otras dos estructuras Rectangle.
		Si no hay intersección, se devuelve un Rectangle vacío.
	*/
	Rectangle Intersect(Rectangle a, Rectangle b) {
		int32_t x1 = max(a.X, b.X);
		int32_t x2 = min(a.X + a.Width, b.X + b.Width);
		int32_t y1 = max(a.Y, b.Y);
		int32_t y2 = min(a.Y + a.Height, b.Y + b.Height);

		if (x2 >= x1 && y2 >= y1)
			return Rectangle(x1, y1, x2 - x1, y2 - y1);
		return Rectangle(0, 0, 0, 0);
	};

	//Reemplaza Rectangle con la intersección entre él mismo y el elemento Rectangle especificado.
	void Intersect(Rectangle rect) {
		Rectangle result = Intersect(rect, *this);
		X = result.X;
		Y = result.Y;
		Width = result.Width;
		Height = result.Height;
	};

	//Determina si este rectángulo tiene una intersección con rect.
	bool IntersectsWith(Rectangle rect) {
		return (rect.X < X + Width) && (X < (rect.X + rect.Width)) && (rect.Y < Y + Height) && (Y < rect.Y + rect.Height);
	};

	//Ajusta la ubicación de este rectángulo en la cantidad especificada.
	inline void Offset(Point P) {
		X += P.X;
		Y += P.Y;
	};

	//Ajusta la ubicación de este rectángulo en la cantidad especificada.
	inline void Offset(int32_t x, int32_t y) {
		X += x;
		Y += y;
	};

	//Convierte los atributos de este Rectangle en una cadena legible.
	String ToString() {
		return String("{X=, Y=, Width=, Height= }");
	};

	//Obtiene una estructura Rectangle que contiene la intersección de dos estructuras Rectangle.
	Rectangle Union(Rectangle a, Rectangle b) {
		int x1 = min(a.X, b.X);
		int x2 = max(a.X + a.Width, b.X + b.Width);
		int y1 = min(a.Y, b.Y);
		int y2 = max(a.Y + a.Height, b.Y + b.Height);

		return Rectangle(x1, y1, x2 - x1, y2 - y1);
	}

	//Devuelve una clase Size con el tamaño del rectángulo
	inline class Size getSize() {
		return Size;
	};

	//Obtiene la coordenada de origen de esta estructura Rectangle.
	inline Point getLocation() {
		return Location;
	};

	//Devuelve una clase Point con las coordenadas de inicio del rectángulo
	inline Point getStartPoint() {
		return Location;
	};

	//Devuelve una clase Point con las coordenadas de fin del rectángulo
	inline Point getEndPoint() {
		return Point(X + Width, Y + Height);
	};

	bool operator== (const Rectangle B) {
		if ((X == B.X) && (Y == B.Y) && (Width == B.Width) && (Height == B.Height))
			return true;
		return false;
	};

	bool operator!= (const Rectangle B) {
		return !(*this == B);
	};

	void Print(Stream* s) {
		s->printf("Rectangle -> X:%i, Y:%i, Width:%i, Height:%i\n", X, Y, Width, Height);
	};

	void Test(Stream* s, Rectangle B) {
		s->printf("RectangleA(%i, %i, %i, %i) RectangleB(%i, %i, %i, %i) ",
			X, Y, Width, Height, B.X, B.Y, B.Width, B.Height);

		s->printf(" %s, %s, %s, %s\n",
			*this == B ? "equal" : "",
			IntersectsWith(B) ? "overlaps" : "",
			Contains(B) ? " B is inside A" : "",
			B.Contains(*this) ? " A is inside B" : "");
	};
};


#endif // !__CTYPES_H
